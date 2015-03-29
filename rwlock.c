#include <u.h>
#include <libc.h>
#include <multitask.h>
#include "multitask-impl.h"

typedef struct QItem QItem;

struct QItem
{
    QItem *next;
    Task *task;
    int rw;
};

enum { QR, QW };

/* l->locked will be [0;INT_MAX-1] when reading and INT_MAX when writing */

void
rlock( RWLock *l )
{
    QItem qi;

    while (1) {
        int e = atomic_load(&l->locked);

        while (e < INT_MAX - 1) {
            if (atomic_compare_exchange_weak(&l->locked, &e, e + 1)) { return; }
        }

        lock(&l->l);
        if (atomic_load(&l->locked) >= INT_MAX - 1) {
            qi.next = nil;
            qi.task = _taskdequeue();
            qi.rw = QR;

            if (l->begin) {
                ((QItem *)l->begin)->next = &qi;
            } else {
                l->begin = &qi;
            }
            l->end = &qi;

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
    QItem *qi = nil;
    int e = atomic_fetch_sub(&l->locked, 1);

    if (e >= INT_MAX - 1) {
        /* try to wake a reader */
        lock(&l->l);
        if ((qi = l->begin) != nil) {
            if (qi->rw == QR) {
                l->begin = qi->next;
            } else {
                qi = nil;
            }
        }
        unlock(&l->l);
    } else if (e == 1) {
        /* try to wake writer */
        lock(&l->l);
        if ((qi = l->begin) != nil) {
            l->begin = qi->next;
        }
        unlock(&l->l);
    }

    if (qi) { _taskready(qi->task); }
}

void
wlock( RWLock *l )
{
    QItem qi;

    while (1) {
        int e = atomic_load(&l->locked);

        while (e == 0) {
            if (atomic_compare_exchange_weak(&l->locked, &e, INT_MAX)) { return; }
        }

        lock(&l->l);
        if (atomic_load(&l->locked) > 0) {
            qi.next = nil;
            qi.task = _taskdequeue();
            qi.rw = QW;

            if (l->begin) {
                ((QItem *)l->begin)->next = &qi;
            } else {
                l->begin = &qi;
            }
            l->end = &qi;

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
    QItem *qi = nil;

    atomic_store(&l->locked, 0);
    lock(&l->l);
    if ((qi = l->begin) != nil) {
        l->begin = qi->next;
    }
    unlock(&l->l);

    if (qi) { _taskready(qi->task); }
}
