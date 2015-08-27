/* sysv amd64 abi:
 * integer arguments 1-6 in RDI, RSI, RDX, RCX, R8, R9
 * integer arguments 7- on the stack
 * return value in RAX
 * RBP, RBX, R12-R15 are callee save
 */

/* void _tasksetjmp(jmp_buf env, void *stack, Task *t) */
TEXT _tasksetjmp
    /* save stack pointer */
    PUSH RBX
    MOV RBX, RSP
    /* switch to new stack */
    MOV RSP, RSI
    /* push arguments to new stack */
    PUSH RDX
    /* save context with setjmp (RDI still contains env) */
    CALL setjmp
    TEST RAX, RAX
    JZ 1f
    /* entering thread for the first time */
    MOV RDI, QWORD PTR [RSP] /* t */
    MOV QWORD PTR [RSP], 0 /* end of frame */
    MOV RBP, 0 /* frame pointer if used */
    JMP _taskstart

    /* restore stack pointer and return */
1:  MOV RSP, RBX
    POP RBX
    RET

/* void _taskspin(void) */
TEXT _taskspin
    PAUSE
    RET
