#include <u.h>
#include <libc.h>
#include "multitask.h"
#include "multitask-impl.h"

#include <sys/syscall.h>

#ifdef SYS_futex
#define usefutex
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
static_assert(sizeof(int) == sizeof(atomic_int),
              "int and atomic_int must be interchangable");
#endif

void
lock( Lock *l )
{
    while (1) {
        int spins = 200;

        while (spins--) {
            if (!atomic_load_explicit(&l->locked, memory_order_acquire)) {
                int c = 0;
                if (atomic_compare_exchange_strong(&l->locked, &c, 1)) {
                    atomic_thread_fence(memory_order_seq_cst);
                    return;
                }
            }
            _taskspin();
        }

#ifdef usefutex
        syscall(SYS_futex, (volatile int *)&l->locked, FUTEX_WAIT, 1, nil);
#else
        nsleep(10000);
#endif
    }
}

int
trylock( Lock *l )
{
    int c = 0;
    return atomic_compare_exchange_strong(&l->locked, &c, 1);
}

void
unlock( Lock *l )
{
    atomic_store_explicit(&l->locked, 0, memory_order_release);
#ifdef usefutex
    syscall(SYS_futex, (volatile int *)&l->locked, FUTEX_WAKE, 1, nil);
#endif
}
