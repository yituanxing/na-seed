.option nopic
.section .text,"ax",@progbits
.globl _start
.p2align 2
_start:
    li a0,1           /* stdout */
    la a1,message
    li a2,6
    li a7,64          /* write */
    ecall
    li t0,6
    bne a0,t0,failed
    li a0,0
    li a7,93          /* exit(0) */
    ecall
failed:
    li a0,1
    li a7,93
    ecall
1:  j 1b
.section .rodata,"a",@progbits
message: .ascii "hello\n"
