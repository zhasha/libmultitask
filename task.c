#include <u.h>
#include <libc.h>
#include "multitask.h"
#include "multitask-impl.h"

#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

struct Tls
{
    Task *cur;
    Task *ready;
    _Atomic(Task *) readyend;
    Task readystub;
    volatile ulong seq;

    uint ntasks;
    bool popped;
    sem_t sem;
    sigset_t oldsigs;
    jmp_buf ptctx;

    void *tlsmem;

    /* signal stack */
    void *sigstack;
};

static thread_local Tls *tasks;

#define DEAD(t) (!t->stack)
#define DIE(t) do { t->stack = nil; } while (0)

void _tasksetjmp(jmp_buf env, void *stack, Task *t);
noreturn void _taskstart(Task *t);

noreturn void
_taskstart( Task *t )
{
    t->fn(t->arg);
    taskexit();
}

static inline bool
enqueue( Task *t )
{
    Task *p;
    bool sig;

    atomic_store(&t->anext, nil);
    p = atomic_exchange(&t->tls->readyend, t);
    sig = (uintptr)p & 1;
    p = (Task *)((uintptr)p & ~(uintptr)1);
    atomic_store(&p->anext, t);

    return sig;
}

void
_taskready( Task *t )
{
    if (enqueue(t)) {
        int r = sem_post(&t->tls->sem);
        assert(r == 0);
    }
}

Task *
_taskdequeue( void )
{
    assert(!tasks->popped);
    tasks->popped = true;
    return tasks->cur;
}

void
_taskundequeue( Task *t )
{
    (void)t;
    assert(tasks->popped);
    assert(t == tasks->cur);
    tasks->popped = false;
}

#if 0
/* This is unnecessary simply because we don't want to use signals, however it
 * has been properly integrated into everything that technically needs it so it
 * could potentially be useful some day. */

static sigset_t allsigs;

static void
allsigsinit( void )
{
    int e = sigfillset(&allsigs);
    assert(e == 0);
}

void
_threadblocksigs( void )
{
    static pthread_once_t oncesigs = PTHREAD_ONCE_INIT;
    int e;

    pthread_once(&oncesigs, allsigsinit);

    e = pthread_sigmask(SIG_SETMASK, &allsigs, &tasks->oldsigs);
    assert(e == 0);
}

void
_threadunblocksigs( void )
{
    int e = pthread_sigmask(SIG_SETMASK, &tasks->oldsigs, nil);
    assert(e == 0);
}
#else
void _threadblocksigs(void) { }
void _threadunblocksigs(void) { }
#endif

static inline Task *
newtask( void (*fn)(void *),
         void *arg,
         size_t stacksize,
         size_t datasize,
         bool nostack )
{
    size_t stksz = 0;
    Task *t;
    byte *mem;

    if (!nostack) {
        if ((size_t)-1 - stacksize >= 63) { stacksize += 63; }
        stacksize &= ~(size_t)63;

        /* overflow check */
        if ((size_t)-1 - stacksize < sizeof(Task)) {
            errno = ENOMEM;
            return nil;
        }
        stksz = stacksize;
    }
    if ((size_t)-1 - stksz - sizeof(Task) < datasize) {
        errno = ENOMEM;
        return nil;
    }

    mem = aligned_alloc(64, stksz + sizeof(Task) + datasize);
    if (!mem) { return nil; }

    t = (Task *)&mem[stksz];
    memset(t, 0, sizeof(*t));
    t->stacksize = stacksize;
    if (!nostack) { t->stack = t; }
    t->fn = fn;
    t->arg = arg;
    t->data = t + 1;
    t->mem = mem;

    /* stack should be aligned to 64-byte boundary */
    assert(((uintptr)t->stack & (uintptr)63) == 0);

    return t;
}

static inline void
freetask( Task *t )
{
    free(t->mem);
}

static inline Task *
rrtask( void )
{
    Task *q, *c = tasks->cur;
    bool calive = !DEAD(c) && !tasks->popped;

    /* really fast single task path */
    if (calive && tasks->ntasks == 1) { return nil; }

    q = tasks->ready;
    while (1) {
        Task *n;

        n = atomic_load(&q->anext);
        if (q == &tasks->readystub) {
            /* we got the stub element so look at the next one */
            if (!n) {
                /* no elements in queue but we're still good to go */
                if (calive) { return nil; }

                /* only the stub element is left in the queue so wait. */
                goto popwait;
            }
            tasks->ready = n;
            q = n;
            n = atomic_load(&n->anext);
        }

        if (n) {
            /* popped an element from the queue */
            tasks->ready = n;
            goto trypopq;
        }

        if (q != atomic_load(&tasks->readyend)) {
            /* Time delay where readyend has been swapped but next hasn't yet.
             * This is a good time to spin as we cannot pop the single element
             * left in the queue without letting this insertion finish */
            _taskspin();
            continue;
        }

        /* push back stub element */
        enqueue(&tasks->readystub);

        /* try to extract element again */
        n = atomic_load(&q->anext);
        if (n) {
            tasks->ready = n;
            goto trypopq;
        }

        if (calive) {
            /* no other tasks in queue but we're ready fast-path */
            return nil;
        }

        /* still don't have an element after all that: wait */
popwait:
        {
            Task *w = (Task *)((uintptr)q | 1);
            Task *a = q;
            if (atomic_compare_exchange_weak(&tasks->readyend, &a, w)) {
                int e = errno; /* taskyield() needs to not clobber errno */
                while (sem_wait(&tasks->sem) != 0) {
                    assert(errno == EINTR);
                }
                errno = e;
            }
            continue;
        }

trypopq:
        /* if q is dead, free it and try again */
        if (DEAD(q)) {
            assert(q != &tasks->readystub);
            freetask(q);
            q = tasks->ready;
            continue;
        }
        /* if c was not popped or is dead, reinsert it into the list. In the
         * first case so that it can be rescheduled and in the latter case so
         * the above code can cull it once we jump away from its stack */
        if (!tasks->popped || DEAD(c)) { enqueue(c); }
        tasks->popped = false;
        return q;
    }
}

static void *
threadstart( void *arg )
{
    Task *t = arg;
    t->stack = &t;
    tasks = t->tls;

    /* switch to new task */
    if (setjmp(t->tls->ptctx) == 0) {
        taskyield();
        t->fn(t->arg);
        taskexit();
    }
    /* after the last exit, we go here */

    /* reap all dead tasks */
    t = tasks->ready;
    while (t) {
        Task *n = atomic_load(&t->anext);
        if (t != &tasks->readystub) { freetask(t); }
        t = n;
    }

    /* should be a nop barring a terrible libc */
    sem_destroy(&tasks->sem);
    /* free alt sig stack (pthread's stack is always large enough, making this
     * as safe as it's going to get when signals are involved) */
    threadsigstack(0);

    /* free memory associated with tls */
    free(tasks->tlsmem);

    return nil;
}

typedef struct Mainargs Mainargs;

struct Mainargs
{
    int argc;
    char **argv;
};

static void
mainstart( void *arg )
{
    Mainargs *a = arg;
    threadmain(a->argc, a->argv);
}

int
main( int argc,
      char *argv[] )
{
    Mainargs arg = { argc, argv };
    Task t;
    Tls tls;

    /* TLS */
    memset(&tls, 0, sizeof(tls));
    tls.cur = &t;
    tls.ready = &tls.readystub;
    atomic_init(&tls.readyend, &tls.readystub);
    atomic_init(&tls.readystub.anext, nil);
    tls.readystub.tls = &tls;
    tls.ntasks = 1;
    tls.popped = false;
    if (sem_init(&tls.sem, 0, 0) != 0) { panic("sem_init failed: %r"); }

    /* Task */
    memset(&t, 0, sizeof(t));
    t.stacksize = (size_t)-1 / 2;
    t.fn = mainstart;
    t.arg = &arg;
    t.tls = &tls;

    /* the main thread has infinite stack, so just give it arbitrary size */
    threadstart(&t);

    /* when main returns it will call exit() but by calling pthread_exit
     * instead we will allow all threads to keep on living after main returns.
     * This way, you need to manually call exit() to kill the pid */
    pthread_exit(nil);
}

int
threadsigstack( size_t stacksize )
{
    stack_t ss;
    void *stk = nil;

    if (stacksize == 0) {
        /* remove alt stack */
        if (!tasks->sigstack) { return 0; }

        ss.ss_sp = nil;
        ss.ss_flags = SS_DISABLE;
        ss.ss_size = 0;
    } else {
        /* exchange alt stack */
        if (stacksize < MINSIGSTKSZ) { stacksize = MINSIGSTKSZ; }

        stk = malloc(stacksize);
        if (!stk) { return -1; }

        ss.ss_sp = stk;
        ss.ss_flags = 0;
        ss.ss_size = stacksize;
    }

    if (sigaltstack(&ss, nil) != 0) { return -1; }
    free(tasks->sigstack);
    tasks->sigstack = stk;

    return 0;
}

Task *
_newthread( pthread_t *pt,
            void (*fn)(void *),
            void *arg,
            size_t stacksize,
            size_t datasize )
{
    pthread_attr_t attr;
    Task *t;
    Tls *tls;

    if ((size_t)-1 - sizeof(Tls) < datasize) {
        errno = ENOMEM;
        return nil;
    }
    if (stacksize < PTHREAD_STACK_MIN) { stacksize = PTHREAD_STACK_MIN; }

    t = newtask(fn, arg, stacksize, sizeof(Tls) + datasize, true);
    if (!t) { return nil; }
    tls = t->data;
    t->tls = tls;
    t->data = tls + 1;

    /* init tls */
    memset(tls, 0, sizeof(*tls));
    tls->cur = t;
    tls->ready = &tls->readystub;
    atomic_init(&tls->readyend, &tls->readystub);
    atomic_init(&tls->readystub.anext, nil);
    tls->readystub.tls = tls;
    tls->ntasks = 1;
    tls->popped = true;
    if (sem_init(&tls->sem, 0, 0) != 0) { goto errout; }

    /* give the pthread proper attributes, including adjusted stack size */
    if (pthread_attr_init(&attr) != 0) { goto errout; }
    if (pthread_attr_setstacksize(&attr, t->stacksize) != 0 ||
        pthread_attr_setguardsize(&attr, 0) != 0 ||
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
        goto attrout;
    }

    if (pthread_create(pt, &attr, threadstart, t) != 0) { goto attrout; }
    pthread_attr_destroy(&attr);
    return t;

attrout:
    pthread_attr_destroy(&attr);
errout:
    freetask(t);
    return nil;
}

int
threadcreate( void (*fn)(void *),
              void *arg,
              size_t stacksize )
{
    pthread_t dummy;
    Task *t;

    t = _newthread(&dummy, fn, arg, stacksize, 0);
    if (!t) { return -1; }
    _taskready(t);

    return 0;
}

int
taskcreate( void (*fn)(void *),
            void *arg,
            size_t stacksize )
{
    Task *t = newtask(fn, arg, stacksize, 0, false);
    if (!t) { return -1; }

    /* put Tls into malloc'd memory so the main thread may exit in peace */
    if (!tasks->tlsmem) {
        tasks->tlsmem = malloc(sizeof(Tls));
        if (!tasks->tlsmem) {
            freetask(t);
            return -1;
        }
        assert(tasks->ready == &tasks->readystub);
        *(Tls *)tasks->tlsmem = *tasks;
        tasks = tasks->tlsmem;
        /* taskcreate can only be called from the singular task in the current
         * thread, thus cur is always valid and never in the hands of another
         * thread, so this is perfectly safe. We also need to update readystub
         * and the queue, which should only contain readystub. */
        tasks->cur->tls = tasks;
        tasks->readystub.tls = tasks;
        tasks->ready = &tasks->readystub;
        atomic_init(&tasks->readyend, &tasks->readystub);
    }
    t->tls = tasks;
    t->tls->ntasks++;

    _tasksetjmp(t->ctx, t->stack, t);
    _taskready(t);

    return 0;
}

size_t
taskstack( void )
{
    byte c = 0;

    return tasks->cur->stacksize - (size_t)((byte *)tasks->cur->stack - &c);
}

ulong
taskyield( void )
{
    Task *t = rrtask();

    /* rrtask will return nil if there's no need to switch contexts */
    if (t) {
        Task *c = tasks->cur;
        ulong seq = tasks->seq++;

        /* swap contexts */
        fegetenv(&c->fctx);
        if (setjmp(c->ctx) == 0) {
            tasks->cur = t;
            longjmp(t->ctx, 1);
        } else {
            fesetenv(&c->fctx);
        }
        return tasks->seq - seq;
    }
    return 0;
}

noreturn void
taskexit( void )
{
    Task *c = tasks->cur;

    /* mark thread as dead so it will be culled later */
    DIE(c);

    if (--tasks->ntasks == 0) {
        /* push self to queue so the OS thread can reap us */
        enqueue(c);
        /* jump back to the OS thread that spawned us */
        longjmp(tasks->ptctx, 1);
    }
    /* perform a non-returning yield */
    taskyield();
    abort();
}
