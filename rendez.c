#include <u.h>
#include <libc.h>
#include <multitask.h>
#include "multitask-impl.h"

#define HTSZ 16

static inline uint
hash( void *p )
{
    byte *d = (byte *)&p;
    uint i, h = 0;

    for (i = 0; i < sizeof(void *); ++i) {
        h = d[i] + (h << 6) + (h << 16) - h;
    }
    return h;
}

void *
rendez( void *tag,
        void *value )
{
    static struct { Task *volatile t; Lock l; } ht[HTSZ];

    uint h = hash(tag) % HTSZ;
    Task *o, *p, *self;

    _threadblocksigs();
    lock(&ht[h].l);
    for (p = nil, o = ht[h].t; o != nil; p = o, o = atomic_load(&o->next)) {
        if (o->rendtag == tag) {
            /* found the same tag, exchange values */
            void *other = o->rendval;
            o->rendval = value;

            if (p) {
                atomic_init(&p->next, atomic_load(&o->next));
            } else {
                ht[h].t = atomic_load(&o->next);
            }

            /* resume the waiting task */
            unlock(&ht[h].l);
            _threadunblocksigs();
            _taskready(o);

            return other;
        }
    }

    /* no such rendezvous tag, so fill one out and insert it */
    self = _taskdequeue();
    self->rendtag = tag;
    self->rendval = value;
    atomic_init(&self->next, ht[h].t);
    ht[h].t = self;

    /* unlock and wait for rendezvous */
    unlock(&ht[h].l);
    _threadunblocksigs();
    taskyield();

    /* return exchanged value */
    return self->rendval;
}

void *
arendez( ARendez *r,
         void *value )
{
    Task *t;

    /* save rendez value before we try to insert ourselves */
    _threadblocksigs();
    t = _taskdequeue();
    t->rendval = value;

    while (1) {
        void *other = nil;

        /* exchange whatever is in the slot for a nil */
        if ((other = atomic_exchange(&r->task, nil)) != nil) {
            Task *o = other;

            /* exchange values */
            other = o->rendval;
            o->rendval = value;

            /* put both tasks back as ready */
            _taskundequeue(t);
            _threadunblocksigs();
            _taskready(o);

            /* return exchanged value */
            return other;
        }

        /* if it was nil to begin with, try to park ourselves in the slot */
        if (atomic_compare_exchange_weak(&r->task, &other, t)) {
            _threadunblocksigs();
            taskyield();

            /* after yield returns the value has been exchanged */
            return t->rendval;
        }
    }
}
