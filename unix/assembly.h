.macro TEXT n global=yes
    .text
    .section .text.\n,"ax",%progbits
    .hidden \n
    .ifnc local,\global
        .globl \n
    .endif
    .type \n,%function
    \n:
.endm

.macro DATA n global=yes
    .data
    .section .data.\n,"aw",%progbits
    .hidden \n
    .ifnc local,\global
        .globl \n
    .endif
    .type \n,%object
    \n:
.endm

.macro RODATA n global=yes
    .rodata
    .section .rodata.\n,"a",%progbits
    .hidden \n
    .ifnc local,\global
        .globl \n
    .endif
    .type \n,%object
    \n:
.endm

.macro BSS n sz global=yes
    .hidden \n
    .ifnc local,\global
        .comm \n,\sz
    .else
        .lcomm \n,\sz
    .endif
.endm

.macro TSYM n global=yes
    .hidden \n
    .ifnc local,\global
        .globl \n
    .endif
    .type \n,%function
    \n:
.endm

.macro DSYM n global=yes
    .hidden \n
    .ifnc local,\global
        .globl \n
    .endif
    .type \n,%object
    \n:
.endm
