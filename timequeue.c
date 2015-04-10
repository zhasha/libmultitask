#include <u.h>
#include <libc.h>
#include <multitask.h>
#include "multitask-impl.h"

#include <pthread.h>

struct Waiter
{
    Chan *c;
    struct timespec ts;
};

enum {
    DEFWAITERS = 16,
};

static inline bool
lt( const Waiter *a,
    const Waiter *b )
{
    return (a->ts.tv_sec <  b->ts.tv_sec) ||
           (a->ts.tv_sec == b->ts.tv_sec && a->ts.tv_nsec < b->ts.tv_nsec);
}

static void
heapifydown( TimeQueue *q,
             size_t i )
{
    Waiter t;
    size_t c, c3;

    t = q->w[i];
    while (1) {
        /* calculate first and third child node offsets */
        c = i * 4 + 1;
        c3 = c + 2;

        if (c >= q->nw) { break; }

        /* find the smallest of the 4 children */
        if (c + 1 < q->nw && lt(&q->w[c + 1], &q->w[c])) { c++; }
        if (c3 < q->nw) {
            if (c3 + 1 < q->nw && lt(&q->w[c3 + 1], &q->w[c3])) { c3++; }
            if (lt(&q->w[c3], &q->w[c])) { c = c3; }
        }

        /* stop if the smallest child element is >= this */
        if (!lt(&q->w[c], &t)) { break; }

        /* swap the elements */
        q->w[i] = q->w[c];
        q->w[c] = t;
        q->w[i].c->tqi = i;
        q->w[c].c->tqi = c;
        i = c;
    }
}

static void
heapifyup( TimeQueue *q,
           size_t i )
{
    Waiter t;
    size_t p;

    t = q->w[i];
    while (i > 0) {
        /* calculate the parent element offset */
        p = (i - 1) / 4;
        /* if it's not larger than us, stop */
        if (!lt(&t, &q->w[p])) { break; }

        /* swap elements */
        q->w[i] = q->w[p];
        q->w[p] = t;
        q->w[i].c->tqi = i;
        q->w[p].c->tqi = p;
        i = p;
    }
}

static inline struct timespec
ns2ts( TimeQueue *q,
       uvlong nsec )
{
    struct timespec ts;
    int r;

    /* get the current time */
    r = clock_gettime(q->clock, &ts);
    assert(r == 0);

    /* add the timeout to it */
#define NS 1000000000
    ts.tv_sec += (time_t)(nsec / NS);
    ts.tv_nsec += (long)(nsec % NS);
    if (ts.tv_nsec > NS) {
        ts.tv_nsec -= NS;
        ts.tv_sec++;
    }
#undef NS

    return ts;
}

void
_tqinsert( TimeQueue *q,
           Chan *c,
           uvlong nsec,
           bool flush )
{
    struct timespec ts;
    int r;
    size_t ti;

    if (atomic_load(&c->closed)) { return; }

    _tqremove(q, c, false, flush);
    if (nsec == 0) { return; }

    ts = ns2ts(q, nsec);

    /* insert timeout on heap */
    r = pthread_mutex_lock(&q->mtx);
    assert(r == 0);
    q->w[q->nw].ts = ts;
    q->w[q->nw].c = c;
    c->tqi = q->nw;
    heapifyup(q, q->nw++);
    ti = c->tqi;
    r = pthread_mutex_unlock(&q->mtx);
    assert(r == 0);

    if (ti == 0) {
        /* this is the shortest so far, so ping the timer thread */
        r = pthread_cond_signal(&q->cond);
        assert(r == 0);
    }
}

void
_tqremove( TimeQueue *q,
           Chan *c,
           bool free,
           bool flush )
{
    size_t i;
    int r;

    r = pthread_mutex_lock(&q->mtx);
    assert(r == 0);

    if (c) {
        i = c->tqi;
        if (i < q->nw && q->w[i].c == c) {
            /* only remove from heap if it's still registered */
            if (i != --q->nw) {
                /* if it's not the last element, we re-heapify */
                q->w[i] = q->w[q->nw];
                q->w[i].c->tqi = i;
                heapifyup(q, i);
                heapifydown(q, i);
            }
        }
    }

    if (free) {
        size_t sz;

        q->nneed--;
        sz = q->nalloc / 4;
        if (q->nneed < sz && sz >= DEFWAITERS / 2) {
            void *mem;
            /* the array grows at a factor of 2, so we shrink when we fall
             * below half of that threshold, ie. 1/4th. This is a fairly good
             * strategy for not hogging too much RAM nor sacrificing speed */
            sz *= 2;
            mem = realloc(q->w, sz * sizeof(Waiter));
            if (mem) {
                q->w = mem;
                q->nalloc = sz;
            }
        }
    }

    if (flush) {
        /* remove potential reply when being reset */
        chanrecvnb(c, nil);
    }

    r = pthread_mutex_unlock(&q->mtx);
    assert(r == 0);

    if (i == 0) {
        int r = pthread_cond_signal(&q->cond);
        assert(r == 0);
    }
}

int
_tqalloc( TimeQueue *q )
{
    int r;

    r = pthread_mutex_lock(&q->mtx);
    assert(r == 0);

    if (q->nneed >= q->nalloc) {
        /* realloc if there's not enough room to run all waiters */
        size_t n = (q->nalloc == 0) ? DEFWAITERS : (q->nalloc * 2);
        void *mem = realloc(q->w, n * sizeof(Waiter));

        if (!mem) {
            r = pthread_mutex_unlock(&q->mtx);
            assert(r == 0);
            return -1;
        }

        q->nalloc = n;
        q->w = mem;
    }
    q->nneed++;

    r = pthread_mutex_unlock(&q->mtx);
    assert(r == 0);

    return 0;
}

static void
timethread( void *arg )
{
    TimeQueue *q = arg;
    Waiter w;
    int r;

    r = pthread_mutex_lock(&q->mtx);
    assert(r == 0);
    while (1) {
        uvlong res;

        /* no timechan waiters, so wait for some */
        while (q->nw == 0 && !q->stop) {
            pthread_cond_wait(&q->cond, &q->mtx);
        }
        if (q->stop) { break; }

        /* wait for the timeout */
        w = q->w[0];
        while (pthread_cond_timedwait(&q->cond, &q->mtx, &w.ts) != ETIMEDOUT) {
            if (q->nw == 0) { break; }
            w = q->w[0];
        }
        if (q->nw == 0 || w.c != q->w[0].c) { continue; }

        /* pop top element */
        q->w[0] = q->w[--q->nw];
        q->w[0].c->tqi = 0;
        heapifydown(q, 0);

        /* notify the channel */
        res = q->cb(w.c);
        if (res > 0) {
            /* reinsert element */
            q->w[q->nw].ts = ns2ts(q, res);
            q->w[q->nw].c = w.c;
            w.c->tqi = q->nw;
            heapifyup(q, q->nw++);
        }
    }
    r = pthread_mutex_unlock(&q->mtx);
    assert(r == 0);

    free(q->w);
}

int
_tqinit( TimeQueue *q,
         uvlong (*cb)(Chan *) )
{
    pthread_condattr_t cattr, *ca = nil;

    memset(q, 0, sizeof(*q));

    /* try to set a better clock source */
    q->clock = CLOCK_REALTIME;
    if (pthread_condattr_init(&cattr) == 0) {
        static const clockid_t clocks[] = {
            /* these clocks are linux-specific. RAW is unaffected by outside
             * stimuli AS CLOCK SHOULD BE and COARSE is just a faster
             * MONOTONIC */
#ifdef CLOCK_MONOTONIC_RAW
            CLOCK_MONOTONIC_RAW,
#endif
#ifdef CLOCK_MONOTONIC_COARSE
            CLOCK_MONOTONIC_COARSE,
#endif
            CLOCK_MONOTONIC
        };
        uint i;

        for (i = 0; i < sizeof(clocks)/sizeof(*clocks); ++i) {
            if (pthread_condattr_setclock(&cattr, clocks[i]) == 0) {
                q->clock = clocks[i];
                break;
            }
        }

        ca = &cattr;
    }

    /* init the condvar with (hopefully) a good clock source */
    if (pthread_cond_init(&q->cond, ca) != 0) { return -1; }
    if (ca) { pthread_condattr_destroy(ca); }

    if (pthread_mutex_init(&q->mtx, nil) != 0) {
        pthread_cond_destroy(&q->cond);
        return -1;
    }
    q->cb = cb;

    /* start the time thread */
    if (threadcreate(timethread, q, 2048) < 0) {
        pthread_cond_destroy(&q->cond);
        pthread_mutex_destroy(&q->mtx);
        return -1;
    }

    return 0;
}

void
_tqfree( TimeQueue *q )
{
    int r;

    r = pthread_mutex_lock(&q->mtx);
    assert(r == 0);
    q->stop = 1;
    r = pthread_mutex_unlock(&q->mtx);
    assert(r == 0);

    r = pthread_cond_signal(&q->cond);
    assert(r == 0);
}
