#include <u.h>
#include <libc.h>
#include "multitask.h"

void
ref(Ref *r)
{
    (void)atomic_fetch_add(&r->refs, 1);
}

ulong
unref(Ref *r)
{
    return atomic_fetch_sub(&r->refs, 1);
}
