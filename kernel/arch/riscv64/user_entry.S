/* Real supervisor-to-user entry and first genuine U-mode trap.
 * This intentionally does not use the x86 register frame or syscall gate. */
.section .rodata.rv_user, "a", @progbits
.p2align 2
.globl nas_rv_user_start
nas_rv_user_start:
    .option push
    .option norvc
    addi a0, zero, 42
    addi a7, zero, 93  /* Linux/RV64 asm-generic exit */
    ecall
1:  jal zero, 1b
    .option pop
.globl nas_rv_user_end
nas_rv_user_end:

.section .text, "ax", @progbits
.p2align 2
.globl nas_rv_jump_user
nas_rv_jump_user:
    la t0, nas_rv_user_trap
    csrw stvec, t0
    csrw sscratch, a3
    csrr t0, sstatus
    /* Clear SPP, SPIE, SIE before privilege drop.
     * ~(0x100 | 0x20 | 0x2) == -291. */
    li t1, -291
    and t0, t0, t1
    csrw sstatus, t0
    csrw sepc, a1
    mv sp, a2
    csrw satp, a0
    sfence.vma zero, zero
    fence.i
    sret

.p2align 2
.globl nas_rv_user_trap
nas_rv_user_trap:
    /* Trap from U: sscratch has kernel stack top, sp is user SP.
     * No nested traps: handler halts after checking syscall identity. */
    csrrw sp, sscratch, sp
    mv a4, a0
    mv a3, a7
    csrr a0, scause
    csrr a1, sstatus
    csrr a2, sepc
    tail nas_rv_user_trap_c
