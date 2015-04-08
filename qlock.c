#include <u.h>
#include <libc.h>
#include <multitask.h>
#include "multitask-impl.h"

void
qlock( QLock *l )
{
    int i;

    if (atomic_exchange(&l->locked, 1) == 0) { return; }

    lock(&l->l);
    if ((i = atomic_exchange(&l->locked, 1)) != 0) {
        Task *t = _taskdequeue();

        t->next = nil;
        if (l->begin) {
            ((Task *)l->end)->next = t;
        } else {
            l->begin = t;
        }
        l->end = t;
    }
    unlock(&l->l);

    /* we will have acquired the lock upon returning from yield */
    if (i != 0) { taskyield(); }
}

int
qtrylock( QLock *l )
{
    if (atomic_exchange(&l->locked, 1) == 0) { return 1; }
    return 0;
}

void
qunlock( QLock *l )
{
    Task *t;

    lock(&l->l);
    t = l->begin;
    if (t) {
        l->begin = t->next;
    } else {
        atomic_store(&l->locked, 0);
    }
    unlock(&l->l);

    if (t) { _taskready(t); }
}
