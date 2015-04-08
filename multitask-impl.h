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

/* prepare the task t for use */
void _tasksetjmp(jmp_buf env, void *stack, Task *t);

/* insert a processor pause */
void _taskspin(void);

/* task entry point */
noreturn void _taskstart(Task *t);

/* insert a task into the ready queue */
void _taskready(Task *t);

/* pop the current task out of readiness */
Task *_taskdequeue(void);

/* for arendez and alike, where you must pop the task but possibly put it back
 * after doing some atomic magic. _taskready is unsuited for this */
void _taskundequeue(Task *t);

/* block (delay) delivery of all signals */
void _threadblocksigs(void);

/* unblock (allow) delivery of all signals */
void _threadunblocksigs(void);

enum {
    CTNORMAL,
    CTTIME,
    CTIO
};

void _chaninit(Chan *c, size_t elemsz, size_t nelem, void *buf, bool heap, int type);
void _tchanfree(Chan *c);
void _iochanfree(Chan *c);

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

    int stop;
};

void _tqinsert(TimeQueue *q, Chan *c, uvlong nsec, bool flush);
void _tqremove(TimeQueue *q, Chan *c, bool free, bool flush);
int _tqalloc(TimeQueue *q);
int _tqinit(TimeQueue *q, uvlong (*cb)(Chan *));
void _tqfree(TimeQueue *q);
