.section .text,"ax",@progbits
.globl _start
.p2align 2
_start:
    /* Both the static ELF PT_LOAD data and its NOBITS region must load.
     * Wrong data/BSS state exits(1), which the kernel test rejects. */
    la t0, rv_magic
    ld a0, 0(t0)
    la t0, rv_zero
    ld t1, 0(t0)
    bnez t1, wrong
    addi a7, zero, 93   /* asm-generic Linux exit */
    ecall
wrong:
    addi a0, zero, 1
    addi a7, zero, 93
    ecall
1:  j 1b

.section .data,"aw",@progbits
.p2align 3
rv_magic: .dword 42

.section .bss,"aw",@nobits
.p2align 3
rv_zero: .skip 16
