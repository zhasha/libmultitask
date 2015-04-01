#include <u.h>
#include <libc.h>
#include <multitask.h>
#include "multitask-impl.h"

static void *const QR = (void *)0;
static void *const QW = (void *)1;

/* l->locked will be [0;INT_MAX-1] when reading and INT_MAX when writing */

void
rlock( RWLock *l )
{
    while (1) {
        int e = atomic_load(&l->locked);

        while (e < INT_MAX - 1) {
            if (atomic_compare_exchange_weak(&l->locked, &e, e + 1)) { return; }
        }

        lock(&l->l);
        if (atomic_load(&l->locked) >= INT_MAX - 1) {
            Task *t, *self = _taskdequeue();
            atomic_init(&self->next, nil);
            self->rendval = QR;

            if ((t = l->begin) != nil) {
                atomic_init(&t->next, self);
            } else {
                l->begin = self;
            }
            l->end = self;

            unlock(&l->l);
            taskyield();
        } else {
            unlock(&l->l);
        }
    }
}

int
tryrlock( RWLock *l )
{
    int e = atomic_load(&l->locked);

    while (e < INT_MAX - 1) {
        if (atomic_compare_exchange_weak(&l->locked, &e, e + 1)) { return 1; }
    }
    return 0;
}

void
runlock( RWLock *l )
{
    Task *t = nil;
    int e = atomic_fetch_sub(&l->locked, 1);

    if (e >= INT_MAX - 1) {
        /* try to wake a reader */
        lock(&l->l);
        if ((t = l->begin) != nil) {
            if (t->rendval == QR) {
                l->begin = atomic_load(&t->next);
            } else {
                t = nil;
            }
        }
        unlock(&l->l);
    } else if (e == 1) {
        /* try to wake writer */
        lock(&l->l);
        if ((t = l->begin) != nil) {
            l->begin = atomic_load(&t->next);
        }
        unlock(&l->l);
    }

    if (t) { _taskready(t); }
}

void
wlock( RWLock *l )
{
    while (1) {
        int e = atomic_load(&l->locked);

        while (e == 0) {
            if (atomic_compare_exchange_weak(&l->locked, &e, INT_MAX)) { return; }
        }

        lock(&l->l);
        if (atomic_load(&l->locked) > 0) {
            Task *t, *self = _taskdequeue();
            atomic_init(&self->next, nil);
            self->rendval = QW;

            if ((t = l->begin) != nil) {
                atomic_init(&t->next, self);
            } else {
                l->begin = self;
            }
            l->end = self;

            unlock(&l->l);
            taskyield();
        } else {
            unlock(&l->l);
        }
    }
}

int
trywlock( RWLock *l )
{
    int e = atomic_load(&l->locked);

    while (e == 0) {
        if (atomic_compare_exchange_weak(&l->locked, &e, INT_MAX)) { return 1; }
    }
    return 0;
}

void
wunlock( RWLock *l )
{
    Task *t = nil;

    atomic_store(&l->locked, 0);
    lock(&l->l);
    if ((t = l->begin) != nil) {
        l->begin = atomic_load(&t->next);
    }
    unlock(&l->l);

    if (t) { _taskready(t); }
}
