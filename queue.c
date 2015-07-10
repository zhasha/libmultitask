#include <u.h>
#include <libc.h>
#include "multitask.h"
#include "multitask-impl.h"

void
qwait( Queue *q )
{
    Task *t = _taskdequeue();
    t->next = nil;

    lock(&q->l);
    if (!q->begin) {
        q->begin = t;
    } else {
        ((Task *)q->end)->next = t;
    }
    q->end = t;
    unlock(&q->l);

    taskyield();
}

ulong
qwake( Queue *q,
       ulong n )
{
    ulong i;

    lock(&q->l);
    for (i = 0; i < n; ++i) {
        Task *t = q->begin;
        if (!t) { break; }

        q->begin = t->next;
        _taskready(t);
    }
    unlock(&q->l);

    return i;
}
