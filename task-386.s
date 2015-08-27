/*
 * i386 ABI
 * arguments are on the stack in reverse order
 * stack must be 16-byte aligned
 * caller fixes stack
 * EAX, ECX, EDX are caller-save
 * everything else is callee-save
 * EAX is return value
 * EBX is GOT for PIC
 */

/* void _tasksetjmp(jmp_buf env, void *stack, Task *t) */
TEXT _tasksetjmp
    /* pull arguments off the stack */
    MOV EAX, DWORD PTR [ESP + 12] /* t */
    MOV ECX, DWORD PTR [ESP +  8] /* stack */
    MOV EDX, DWORD PTR [ESP +  4] /* env */
    /* save the callee-save local var regs */
    PUSH EDI
    PUSH ESI
    /* switch to and align new stack */
    MOV ESI, ESP
    LEA ESP, [ECX - 12]
    /* put env on the stack as an arg and save t */
    MOV DWORD PTR [ESP], EDX
    MOV EDI, EAX
    /* save context with setjmp */
    CALL setjmp
    TEST EAX, EAX
    JZ 1f
    /* entering thread, put t in the argument position */
    MOV DWORD PTR [ESP], EDI
    PUSH 0 /* alignment and stop block */
    MOV EBP, 0 /* frame pointer if used */
    JMP _taskstart

    /* restore stack pointer and return */
1:  MOV ESP, ESI
    POP ESI
    POP EDI
    RET

/* void _taskspin(void) */
TEXT _taskspin
    PAUSE /* on pre-SSE2, this is "rep nop" aka. nop */
    RET
