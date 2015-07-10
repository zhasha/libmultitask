typedef struct Task Task;
typedef struct Tls Tls;

struct Task
{
    union {
        _Atomic(Task *) anext;
        Task *volatile next;
    };
    Tls *tls;

    /* stack context */
    jmp_buf ctx;
    fenv_t fctx;
    size_t stacksize;
    void *stack;

    /* synchronization stuff */
    union {
        int qtype;
        ssize_t ioret;
        struct {
            void *volatile rendtag;
            void *volatile rendval;
        };
        struct {
            void (*fn)(void *);
            void *arg;
        };
    };

    /* pointer returned by malloc */
    void *mem;
};

/* insert a processor pause */
void _taskspin(void);

/* insert a task into the ready queue */
void _taskready(Task *t);

/* pop the current task out of readiness.
 * for arendez and alike, where you must pop the task but possibly put it back
 * after doing some atomic magic. _taskready is unsuited for this */
Task *_taskdequeue(void);
void _taskundequeue(Task *t);

/* (un)block (delay) delivery of all signals */
void _threadblocksigs(void);
void _threadunblocksigs(void);

void _chaninit(Chan *c, size_t elemsz, size_t nelem, void *buf, void (*dtor)(Chan *));

typedef struct TimeQueue TimeQueue;
typedef struct Waiter Waiter;

struct TimeQueue
{
    pthread_cond_t cond;
    pthread_mutex_t mtx;
    clockid_t clock;

    Waiter *w;
    size_t nw;
    size_t nalloc;
    size_t nneed;

    uvlong (*cb)(Chan *c);

    volatile int stop;
};

enum {
    TQfree = 1,
};

void _tqlock(TimeQueue *q);
void _tqunlock(TimeQueue *q, int sig);
int _tqinsert(TimeQueue *q, Chan *c, uvlong nsec);
int _tqremove(TimeQueue *q, Chan *c, int flags);
int _tqalloc(TimeQueue *q);
int _tqinit(TimeQueue *q, uvlong (*cb)(Chan *));
void _tqfree(TimeQueue *q);
