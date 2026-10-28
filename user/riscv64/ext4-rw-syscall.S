.option nopic
.section .text,"ax",@progbits
.globl _start
.p2align 2
_start:
    call guest_main
1:  j 1b
.globl guest_syscall
guest_syscall:
    la t0,guest_sys_numbers
    slli t1,a0,3
    add t0,t0,t1
    ld a7,0(t0)
    mv a0,a1
    mv a1,a2
    mv a2,a3
    mv a3,a4
    ecall
    ret
.section .rodata,"a",@progbits
.balign 8
guest_sys_numbers: .dword 56,63,64,82,62,57,93,61,34,38,50,17,36,35,79
