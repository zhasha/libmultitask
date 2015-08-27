/*
 * ARM ABI:
 * a1-a4 are arguments (clobbered, caller-save)
 * v1-v6 are callee-save
 * a1 is return value
 */

/* void _tasksetjmp(jmp_buf env, void *stack, Task *t) */
TEXT _tasksetjmp
    /* save stack pointer */
    push {v1, v2, lr}
    mov v1, sp
    /* switch to new stack */
    mov sp, a2
    /* save argument t in v2 */
    mov v2, a3
    /* save context with setjmp (a1 still contains env) */
    bl setjmp
    cmp a1, #0
    beq 1f
    /* entering thread for the first time */
    mov a1, v2
    b _taskstart

    /* restore sp, registers and return */
1:  mov sp, v1
    pop {v1, v2, lr}
    tst lr, #1
    moveq pc, lr
    bx lr

/* void _taskspin(void) */
TEXT _taskspin
    tst lr, #1
    moveq pc, lr
    bx lr
