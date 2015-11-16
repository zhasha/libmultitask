#include <u.h>
#include <libc.h>
#include "multitask.h"
#include "multitask-impl.h"

void
condwait( Cond *c )
{
    Task *t, *n;
    
    t = _taskdequeue();
    t->next = nil;

    lock(&c->l->l);

    /* add ourselves to cond waiters */
    if (c->begin) {
        ((Task *)c->end)->next = t;
    } else {
        c->begin = t;
    }
    c->end = t;

    /* check the qlock queue */
    n = c->l->begin;
    if (n) {
        /* if someone is waiting, let them continue */
        c->l->begin = n->next;
    } else {
        /* otherwise just unlock the qlock */
        atomic_store(&c->l->locked, 0);
    }
    c->end = t;
    c->waiters++;
    unlock(&c->l->l);

    if (n) { _taskready(n); }

    /* wait on cond */
    taskyield();
}

ulong
condsignal( Cond *c )
{
    Task *t;

    lock(&c->l->l);

    /* check that anyone is waiting */
    t = c->begin;
    if (!t) {
        unlock(&c->l->l);
        return 0;
    }

    /* take waiter out of cond queue and put in front of qlock queue */
    c->begin = t->next;
    t->next = c->l->begin;
    if (!c->l->begin) { c->l->end = t; }
    c->l->begin = t;
    c->waiters--;

    unlock(&c->l->l);

    return 1;
}

ulong
condbroadcast( Cond *c )
{
    Task *b, *e;
    ulong waiters;

    lock(&c->l->l);

    /* check that anyone is waiting, same as signal */
    b = c->begin;
    if (!b) {
        unlock(&c->l->l);
        return 0;
    }

    /* move /the entire queue/ into the qlock queue */
    e = c->end;
    e->next = c->l->begin;
    c->begin = nil;
    if (!c->l->begin) { c->l->end = e; }
    c->l->begin = b;
    waiters = c->waiters;
    c->waiters = 0;

    unlock(&c->l->l);

    return waiters;
}
