.section .text,"ax",%progbits
.global _start
.balign 4
_start:
    mov x0,#1
    adr x1,message
    mov x2,#6
    mov x8,#64     /* write */
    svc #0
    cmp x0,#6
    b.ne fail
    mov x0,#0
    mov x8,#93     /* exit */
    svc #0
fail:
    mov x0,#1
    mov x8,#93
    svc #0
1:  b 1b
.section .rodata,"a",%progbits
message: .ascii "hello\n"
