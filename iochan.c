#include <u.h>
#include <libc.h>
#include <multitask.h>
#include "multitask-impl.h"

#include <semaphore.h>
#include <pthread.h>
#include <signal.h>

typedef struct IOThread IOThread;

struct IOThread
{
    struct {
        _Atomic(uint32) lap;
        ssize_t val;
    } buf;

    /* state stuff */
    atomic_int state;
    volatile uvlong timeout;
    Task *volatile canceler;
    volatile int alarm;
    sem_t sem;

    /* itc */
    Task *volatile task;
    volatile IOFunc proc;
    byte argbuf[128];

    /* pthread stuff */
    pthread_t ptid;
};

enum {
    WAITING = -1,
    RUNNING = 0, /* doubles as cancel atomic */
    CANCELED = 1,
    MORIBUND = 2,
};

enum {
    DEFTIMEOUT = 10000,
};

static sigset_t sigs;
static TimeQueue cancelq;
static int SIGCANCEL;

static void dtor(Chan *c);

static void
iothread( void *arg )
{
    IOThread io;
    Chan c;

    memset(&io, 0, sizeof(io));
    _chaninit(&c, sizeof(ssize_t), 1, &io, dtor);

    /* initialize everything and dequeue ourselves */
    atomic_init(&io.state, WAITING);
    io.task = _taskdequeue();
    io.ptid = pthread_self();

    {
        int r;
        /* according to posix these can't fail lest you give bad args */
        r = sem_init(&io.sem, 0, 0);
        assert(r == 0);
        r = pthread_sigmask(SIG_SETMASK, &sigs, nil);
        assert(r == 0);
    }

    /* can't use rendez because of _taskdequeue */
    ((Task *)arg)->rendval = &c;
    _taskready(arg);

    while (1) {
        IOFunc proc;
        ssize_t r;

        taskyield();

        proc = io.proc;
        if (!proc) { break; }
        r = proc(io.argbuf, &io.state);

        io.task = _taskdequeue();
        switch (atomic_exchange(&io.state, WAITING)) {
            case MORIBUND:
                /* this looks racy but since iocall doesn't actually tell you
                 * whether or not it was successful it just ensures that no one
                 * screws with our Task pointer after this */
                if (atomic_exchange(&io.state, MORIBUND) == WAITING) {
                    _taskundequeue(io.task);
                } else {
                    taskyield();
                }
                proc = nil;
                /* fall through */
            case CANCELED:
                /* we must send before rendezvousing with the canceler */
                if (proc) { chansendnb(&c, &r); }

                /* this request was cancelled so someone is waiting for us */
                while (sem_wait(&io.sem) != 0) {
                    assert(errno == EINTR);
                }
                if (io.canceler) { _taskready(io.canceler); }
                break;

            default:
                /* We send non-blocking for a couple of reasons. First off it
                 * doesn't even make sense to block here. Second, since we've
                 * dequeue'd ourselves already, blocking would be illegal */
                chansendnb(&c, &r);
                break;
        }
        if (!proc) { break; }
    }

    /* wait before destroying everything */
    while (sem_wait(&io.sem) != 0) {
        assert(errno == EINTR);
    }

    {
        int r;

        /* remove own cancel queue slot */
        _tqlock(&cancelq);
        r = _tqremove(&cancelq, &c, TQfree);
        _tqunlock(&cancelq, r);

        /* on sane systems this is a nop */
        r = sem_destroy(&io.sem);
        assert(r == 0);
    }
}

static uvlong
cancelcb( Chan *c )
{
    IOThread *io = c->buf;
    int s = atomic_load(&io->state);

    if (s >= CANCELED) {
        /* the basic idea here is to do exponential backoff signalling
         * until the cancellation is successful */
        pthread_kill(io->ptid, SIGCANCEL);
        return io->timeout *= 2;
    } else if (s == RUNNING && io->alarm) {
        /* we reuse the cancelq for alarm signals */
        if (!atomic_compare_exchange_strong(&io->state, &s, CANCELED)) {
            return 0;
        }

        io->timeout = DEFTIMEOUT;
        io->canceler = nil;
        s = sem_post(&io->sem);
        assert(s == 0);

        pthread_kill(io->ptid, SIGCANCEL);
        return DEFTIMEOUT;
    }

    return 0;
}

static void
xsig( int sig )
{
    (void)sig;
    /* empty because signals can go fuck a goat */
}

static int
init( void )
{
    static atomic_int inited;
    static Lock initlock;

    if (atomic_load(&inited) == 0) {
        lock(&initlock);
        if (atomic_load(&inited) == 0) {
            struct sigaction sa;
            int r;

            /* init cancellation time queue */
            if (_tqinit(&cancelq, cancelcb) != 0) {
                unlock(&initlock);
                return -1;
            }
            SIGCANCEL = SIGRTMAX - 1;

            /* create a sigset with only SIGCANCEL unblocked */
            r = sigfillset(&sigs);
            assert(r == 0);
            r = sigdelset(&sigs, SIGCANCEL);
            assert(r == 0);

            /* register a dummy signal handler */
            sa.sa_handler = xsig;
            sa.sa_flags = SA_ONSTACK;
            r = sigfillset(&sa.sa_mask);
            assert(r == 0);
            r = sigaction(SIGCANCEL, &sa, nil);
            assert(r == 0);

            atomic_store(&inited, 1);
        }
        unlock(&initlock);
    }

    return 0;
}

Chan *
iochan( size_t extrastack )
{
    Task *self;

    /* initialize signal stuff */
    if (init() != 0) { return nil; }
    if (_tqalloc(&cancelq) != 0) { return nil; }

    /* dequeue self and wait for the created thread to put us back */
    self = _taskdequeue();

    /* create the thread. The thread will init the channel */
    if (threadcreate(iothread, self, PTHREAD_STACK_MIN + extrastack) < 0) {
        _taskundequeue(self);
        _tqremove(&cancelq, nil, TQfree);
        return nil;
    }

    /* wait for the thread to be created and initialized */
    taskyield();
    return self->rendval;
}

static inline bool
_iocancel( Chan *c,
           int state )
{
    IOThread *io = c->buf;
    int r = RUNNING;

    if (!atomic_compare_exchange_strong(&io->state, &r, state)) {
        return false;
    }

    /* synchronize with the io thread */
    io->canceler = _taskdequeue();
    r = sem_post(&io->sem);
    assert(r == 0);

    pthread_kill(io->ptid, SIGCANCEL);

    /* do the timeout dance */
    _tqlock(&cancelq);
    r |= _tqremove(&cancelq, c, 0);
    io->timeout = DEFTIMEOUT;
    r |= _tqinsert(&cancelq, c, DEFTIMEOUT);
    _tqunlock(&cancelq, r);

    taskyield();

    return true;
}

static void
dtor( Chan *c )
{
    IOThread *io = c->buf;
    int r;

    while (!_iocancel(c, MORIBUND)) {
        r = WAITING;
        if (atomic_compare_exchange_strong(&io->state, &r, MORIBUND)) {
            io->proc = nil;
            _taskready(io->task);
            break;
        }
        _taskspin();
    }

    /* _iocancel accesses members of c after sem_post which allows the thread
     * to continue, rendezvous and destroy itself, so we need this sync or to
     * degrade performance by moving sem_post down in _iocancel to just before
     * task_yield(). */
    r = sem_post(&io->sem);
    assert(r == 0);
}

/* TODO: When musl gets reliable cancellation, use that */
ssize_t
iocancel( Chan *c )
{
    ssize_t ret = 0;

    if (atomic_load(&c->closed)) { return 0; }

    _iocancel(c, CANCELED);
    if (chanrecvnb(c, &ret) != 1) { return 0; }
    return ret;
}

int
ioalarm( Chan *c,
         uvlong timeout )
{
    IOThread *io = c->buf;
    int r = 0;

    if (atomic_load(&io->state) != RUNNING) { return 1; }
    if (atomic_load(&c->closed)) { return 1; }

    _tqlock(&cancelq);
    if (atomic_load(&io->state) != RUNNING) {
        _tqunlock(&cancelq, r);
        return 1;
    }

    r |= _tqremove(&cancelq, c, 0);
    if (timeout > 0) {
        io->alarm = 1;
        r |= _tqinsert(&cancelq, c, timeout);
    } else {
        io->alarm = 0;
    }
    _tqunlock(&cancelq, r);

    return 0;
}

void
iocall( Chan *c,
        IOFunc proc,
        const void *args,
        size_t argsz )
{
    IOThread *io = c->buf;
    Task *t;
    int r = WAITING;

    assert(argsz <= sizeof(io->argbuf));

    /* only one task may use the iochan at a time. Not properly managing this
     * yourself means you're doing something wrong. */
    if (!atomic_compare_exchange_strong(&io->state, &r, RUNNING)) { return; }

    /*
     * race between RUNNING and previous ioalarm.
     * A calls iocall()
     * IO runs
     * A calls ioalarm()
     * IO returns
     * A calls iocall()
     * B is called back by previous ioalarm()
     * B sees io->alarm == 1 && io->state == RUNNING and cancels unrelated call
     *
     * To avoid this we clear the cancel state after setting io->alarm = 0.
     * It's not pretty but it works. It can eat an iocancel or ioalarm call but
     * not after this call returns.
     */
    io->alarm = 0;
    atomic_store(&io->state, RUNNING);
    io->proc = proc;
    if (argsz > 0) { memcpy(io->argbuf, args, argsz); }

    /* when waiting the iothread simply pops its only task. We put it back */
    t = io->task;
    assert(t);
    _taskready(t);
}

typedef struct IOOpen IOOpen;
typedef struct IOOp IOOp;
typedef struct IONSleep IONSleep;
typedef struct IOWait IOWait;

struct IOOpen
{
    const char *pathname;
    int flags;
    mode_t mode;
};

struct IOOp
{
    int rdwr;
    int fd;
    union {
        void *buf;
        const void *cbuf;
    };
    size_t count;
    off_t offset;
};

struct IOWrite
{
    int fd;
    const void *buf;
    size_t count;
    off_t offset;
};

struct IONSleep
{
    uvlong ns;
    uvlong *left;
};

struct IOWait
{
    pid_t pid;
    int *status;
    int options;
};

static ssize_t
xioopen( void *args,
         atomic_int *cancel )
{
    IOOpen *a = args;
    int r = (a->flags & O_CREAT
#ifdef O_TMPFILE
            || (a->flags & O_TMPFILE) == O_TMPFILE
#endif
            ) ?
        open(a->pathname, a->flags, a->mode) :
        open(a->pathname, a->flags);
    (void)cancel;
    return (r < 0) ? (-errno) : r;
}

static ssize_t
xioop( void *args,
       atomic_int *cancel )
{
    IOOp *a = args;
    ssize_t r = a->rdwr ?
        write(a->fd, a->cbuf, a->count) :
        read(a->fd, a->buf, a->count);
    (void)cancel;
    return (r < 0) ? (-errno) : r;
}

static ssize_t
xioopn( void *args,
        atomic_int *cancel )
{
    IOOp *a = args;
    size_t n = 0;

    while (!atomic_load(cancel) && n < a->count) {
        ssize_t r = a->rdwr ?
            write(a->fd, (const char *)a->cbuf + n, a->count - n) :
            read(a->fd, (char *)a->buf + n, a->count - n);
        if (r < 0) {
            if (errno == EINTR) { continue; }
            return -errno;
        }
        if (r == 0) { break; }
        n += (size_t)r;
    }

    return (ssize_t)n;
}

static ssize_t
xionsleep( void *args,
           atomic_int *cancel )
{
    IONSleep *a = args;
    uvlong r = nsleep(a->ns);
    (void)cancel;
    if (a->left) { *a->left = r; }
    return (r == 0) ? 0 : (-1);
}

static ssize_t
xiowait( void *args,
         atomic_int *cancel )
{
    IOWait *a = args;
    pid_t r = waitpid(a->pid, a->status, a->options);
    (void)cancel;
    return (r < 0) ? (-errno) : r;
}

void
ioopen( Chan *c,
        const char *pathname,
        int flags,
        mode_t mode )
{
    IOOpen a = {
        .pathname = pathname,
        .flags = flags,
        .mode = mode
    };
    iocall(c, xioopen, &a, sizeof(a));
}

void
ioread( Chan *c,
        int fd,
        void *buf,
        size_t count )
{
    IOOp a = {
        .rdwr = 0,
        .fd = fd,
        .buf = buf,
        .count = count,
    };
    iocall(c, xioop, &a, sizeof(a));
}

void
ioreadn( Chan *c,
         int fd,
         void *buf,
         size_t count )
{
    IOOp a = {
        .rdwr = 0,
        .fd = fd,
        .buf = buf,
        .count = count,
    };
    iocall(c, xioopn, &a, sizeof(a));
}

void
iowrite( Chan *c,
         int fd,
         const void *buf,
         size_t count )
{
    IOOp a = {
        .rdwr = 1,
        .fd = fd,
        .cbuf = buf,
        .count = count,
    };
    iocall(c, xioop, &a, sizeof(a));
}

void
iowriten( Chan *c,
          int fd,
          const void *buf,
          size_t count )
{
    IOOp a = {
        .rdwr = 1,
        .fd = fd,
        .cbuf = buf,
        .count = count,
    };
    iocall(c, xioopn, &a, sizeof(a));
}

void
ionsleep( Chan *c,
          uvlong ns,
          uvlong *left )
{
    IONSleep a = {
        .ns = ns,
        .left = left
    };
    if (left) { *left = ns; }
    iocall(c, xionsleep, &a, sizeof(a));
}

void
iowait( Chan *c,
        pid_t pid,
        int *status,
        int options )
{
    IOWait a = {
        .pid = pid,
        .status = status,
        .options = options
    };
    iocall(c, xiowait, &a, sizeof(a));
}
