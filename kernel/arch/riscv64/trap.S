/* Full shared-kernel RV64 U-mode entry + Linux syscall trap.
 * M0 exit(42) remains independent hardware regression, not a second OS.
 * Synchronous ECALL only; unexpected faults fail in the shared trap C.
 */
.section .text,"ax",@progbits
.p2align 2
.global nas_arch_enter_user
nas_arch_enter_user:
    la t0, nas_rv_full_trap
    csrw stvec, t0
    /* First entry and exec use the active task's assigned entry stack. */
    la t0, nas_rv_entry_stack_top
    ld t0, 0(t0)
    csrw sscratch, t0
    csrr t0, sstatus
    li t1, -259           /* clear SPP(8), SIE(1) */
    and t0,t0,t1
    ori t0,t0,0x20       /* SPIE so U resumes with interrupts masked by SIE */
    csrw sstatus,t0
    csrw sepc,a0
    mv sp,a1
    li tp,0
    li a0,0
    sret

.p2align 2
.global nas_rv_full_trap
nas_rv_full_trap:
    /* sscratch = supervisor kernel stack top, sp = user stack. */
    csrrw sp, sscratch, sp
    addi sp,sp,-272
    sd zero,0(sp)
    sd x1, 8(sp)
    sd x3, 24(sp)
    sd x4, 32(sp)
    sd x5, 40(sp)
    sd x6, 48(sp)
    sd x7, 56(sp)
    sd x8, 64(sp)
    sd x9, 72(sp)
    sd x10, 80(sp)
    sd x11, 88(sp)
    sd x12, 96(sp)
    sd x13, 104(sp)
    sd x14, 112(sp)
    sd x15, 120(sp)
    sd x16, 128(sp)
    sd x17, 136(sp)
    sd x18, 144(sp)
    sd x19, 152(sp)
    sd x20, 160(sp)
    sd x21, 168(sp)
    sd x22, 176(sp)
    sd x23, 184(sp)
    sd x24, 192(sp)
    sd x25, 200(sp)
    sd x26, 208(sp)
    sd x27, 216(sp)
    sd x28, 224(sp)
    sd x29, 232(sp)
    sd x30, 240(sp)
    sd x31, 248(sp)
    csrr t0,sscratch
    sd t0,16(sp)
    csrr t0,sepc
    sd t0,256(sp)
    csrr t0,sstatus
    sd t0,264(sp)
    mv a0,sp
    call nas_rv_full_trap_c
    /* Copy the finished trap snapshot to the selected task's private stack
     * BEFORE restoring GPRs. No live kernel C frames cross this boundary. */
    mv a0, sp
    call nas_rv_return_stack
    mv sp, a0
nas_rv_resume_restore:
    ld t0,256(sp)
    csrw sepc,t0
    ld t0,264(sp)
    csrw sstatus,t0
    ld x1, 8(sp)
    ld x3, 24(sp)
    ld x4, 32(sp)
    ld x6, 48(sp)
    ld x7, 56(sp)
    ld x8, 64(sp)
    ld x9, 72(sp)
    ld x10, 80(sp)
    ld x11, 88(sp)
    ld x12, 96(sp)
    ld x13, 104(sp)
    ld x14, 112(sp)
    ld x15, 120(sp)
    ld x16, 128(sp)
    ld x17, 136(sp)
    ld x18, 144(sp)
    ld x19, 152(sp)
    ld x20, 160(sp)
    ld x21, 168(sp)
    ld x22, 176(sp)
    ld x23, 184(sp)
    ld x24, 192(sp)
    ld x25, 200(sp)
    ld x26, 208(sp)
    ld x27, 216(sp)
    ld x28, 224(sp)
    ld x29, 232(sp)
    ld x30, 240(sp)
    ld x31, 248(sp)
    /* The kernel may have scheduled a DIFFERENT task during this ECALL.
     * x2 saved in the selected task's frame is the authoritative user SP.
     * sscratch still contains the interrupted OLD task's user SP. If we
     * don't replace it here, the final csrrw resumes the parent on the
     * child's user stack after fork/exit, corrupting libc and triggering
     * user abort/EBREAK (scause=3) even when TLS and PTEs are valid.
     * Do the CSR update before restoring t0/x5 from its saved frame. */
    ld t0, 16(sp)
    csrw sscratch,t0
    ld x5,40(sp)
    addi sp,sp,272
    csrrw sp,sscratch,sp
    sret

/* Only active while the scheduler is waiting on its dedicated stack.
 * Preserve the asynchronous ABI-clobbered registers; C preserves s0-s11.
 * Unlike user traps, sp is ALREADY a valid supervisor stack and sscratch
 * still belongs to a suspended Task: never swap them here. */
.p2align 2
.global nas_rv_idle_trap
nas_rv_idle_trap:
    addi sp,sp,-128
    sd ra,0(sp)
    sd t0,8(sp)
    sd t1,16(sp)
    sd t2,24(sp)
    sd a0,32(sp)
    sd a1,40(sp)
    sd a2,48(sp)
    sd a3,56(sp)
    sd a4,64(sp)
    sd a5,72(sp)
    sd a6,80(sp)
    sd a7,88(sp)
    sd t3,96(sp)
    sd t4,104(sp)
    sd t5,112(sp)
    sd t6,120(sp)
    call nas_rv_idle_irq_c
    ld ra,0(sp)
    ld t0,8(sp)
    ld t1,16(sp)
    ld t2,24(sp)
    ld a0,32(sp)
    ld a1,40(sp)
    ld a2,48(sp)
    ld a3,56(sp)
    ld a4,64(sp)
    ld a5,72(sp)
    ld a6,80(sp)
    ld a7,88(sp)
    ld t3,96(sp)
    ld t4,104(sp)
    ld t5,112(sp)
    ld t6,120(sp)
    addi sp,sp,128
    sret

.p2align 2
.global nas_rv_save_fp
nas_rv_save_fp:
    fsd f0, 0(a0)
    fsd f1, 8(a0)
    fsd f2, 16(a0)
    fsd f3, 24(a0)
    fsd f4, 32(a0)
    fsd f5, 40(a0)
    fsd f6, 48(a0)
    fsd f7, 56(a0)
    fsd f8, 64(a0)
    fsd f9, 72(a0)
    fsd f10, 80(a0)
    fsd f11, 88(a0)
    fsd f12, 96(a0)
    fsd f13, 104(a0)
    fsd f14, 112(a0)
    fsd f15, 120(a0)
    fsd f16, 128(a0)
    fsd f17, 136(a0)
    fsd f18, 144(a0)
    fsd f19, 152(a0)
    fsd f20, 160(a0)
    fsd f21, 168(a0)
    fsd f22, 176(a0)
    fsd f23, 184(a0)
    fsd f24, 192(a0)
    fsd f25, 200(a0)
    fsd f26, 208(a0)
    fsd f27, 216(a0)
    fsd f28, 224(a0)
    fsd f29, 232(a0)
    fsd f30, 240(a0)
    fsd f31, 248(a0)
    frcsr t0
    sd t0,256(a0)
    ret
.p2align 2
.global nas_rv_restore_fp
nas_rv_restore_fp:
    fld f0, 0(a0)
    fld f1, 8(a0)
    fld f2, 16(a0)
    fld f3, 24(a0)
    fld f4, 32(a0)
    fld f5, 40(a0)
    fld f6, 48(a0)
    fld f7, 56(a0)
    fld f8, 64(a0)
    fld f9, 72(a0)
    fld f10, 80(a0)
    fld f11, 88(a0)
    fld f12, 96(a0)
    fld f13, 104(a0)
    fld f14, 112(a0)
    fld f15, 120(a0)
    fld f16, 128(a0)
    fld f17, 136(a0)
    fld f18, 144(a0)
    fld f19, 152(a0)
    fld f20, 160(a0)
    fld f21, 168(a0)
    fld f22, 176(a0)
    fld f23, 184(a0)
    fld f24, 192(a0)
    fld f25, 200(a0)
    fld f26, 208(a0)
    fld f27, 216(a0)
    fld f28, 224(a0)
    fld f29, 232(a0)
    fld f30, 240(a0)
    fld f31, 248(a0)
    ld t0,256(a0)
    fscsr t0
    ret

.section .bss
.balign 16
.global kernel_stack_top
kernel_stack: .skip 16384
kernel_stack_top:

/* First dispatcher entry: resume authentic saved RV64 U-mode registers. */
.section .text,"ax",@progbits
.p2align 2
.global nas_rv_user_resume_frame
nas_rv_user_resume_frame:
    mv sp,a0
    j nas_rv_resume_restore
