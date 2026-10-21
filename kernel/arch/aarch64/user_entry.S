/* Actual ARMv8 EL1 -> EL0 execution and synchronous SVC exception.
 * Non-EL0/async exceptions deliberately fail instead of pretending support.
 * A 2KiB VBAR_EL1 vector table has 16 slots of 128 bytes each.
 * Slot 8 at offset 0x400 is Lower EL using AArch64, synchronous. */
.section .rodata.arm_user,"a",%progbits
.balign 4
.global nas_arm_user_code
nas_arm_user_code:
    mov x0, #42
    mov x8, #93     // asm-generic Linux exit
    svc #0
1:  b 1b
.global nas_arm_user_code_end
nas_arm_user_code_end:

.section .text,"ax",%progbits
.balign 2048
.global nas_arm_vectors
nas_arm_vectors:
.rept 8
    b nas_arm_user_bad_vector
    .balign 128
.endr
    b nas_arm_user_sync
    .balign 128
.rept 7
    b nas_arm_user_bad_vector
    .balign 128
.endr

/* Trace boundaries isolate faults before the first ERET. The saved
 * argument registers are restored because the entry never returns. */
.macro ARM_TRACE stage
    stp x0, x1, [sp, #-32]!
    str x2, [sp, #16]
    mov x0, #\stage
    bl nas_arm_user_trace_stage
    ldr x2, [sp, #16]
    ldp x0, x1, [sp], #32
.endm

/* x0=physical user TTBR0 root, x1=EL0 PC, x2=EL0 SP.
 * Bootloader's TTBR1 high-half kernel mappings remain intact. */
.balign 4
.global nas_arm_enter_el0
nas_arm_enter_el0:
    ARM_TRACE 0
    /* Limine currently transfers control in EL1t (SPSel=0), with the
     * boot stack in SP_EL0 and SP_EL1 not established for exceptions.
     * Copy the live kernel stack to SP_EL1 and select EL1h BEFORE using
     * SP_EL0 for userspace; lower-EL vectors always run on SP_EL1. */
    mov x9, sp
    msr sp_el1, x9
    msr spsel, #1
    isb
    ARM_TRACE 1
    msr daifset, #0xf
    adrp x9, nas_arm_vectors
    add x9, x9, :lo12:nas_arm_vectors
    msr vbar_el1, x9
    isb
    ARM_TRACE 1

    /* Leave Limine's existing kernel MAIR entries 0..6 unchanged.
     * User descriptors select AttrIndx=7, which we set to normal WB. */
    mrs x9, mair_el1
    mov x10, #0xff
    lsl x10, x10, #56
    orr x9, x9, x10
    msr mair_el1, x9
    ARM_TRACE 2

    /* TTBR0_EL1 uses 39-bit / 3-level, TG0=4KiB, EPD0=0.
     * Only TTBR0 fields change; preserve bootloader's TTBR1 setup. */
    mrs x9, tcr_el1
    bic x9, x9, #0x3f
    mov x10, #25
    orr x9, x9, x10
    bic x9, x9, #0x80
    bic x9, x9, #0xc000
    msr tcr_el1, x9
    ARM_TRACE 3
    msr ttbr0_el1, x0
    ARM_TRACE 4
    dsb ishst
    isb
    tlbi vmalle1is
    dsb ish
    isb
    ARM_TRACE 5
    /* Diagnose which side of ERET reaches the exception vectors.
     * Preserve EL0 PC/SP around the one final EL1 logging call. */
    stp x1, x2, [sp, #-16]!
    bl nas_arm_user_last_el1_trace
    ldp x1, x2, [sp], #16
    msr sp_el0, x2
    msr elr_el1, x1
    mov x9, #0x3c0       // mask D/A/I/F, target EL0t (M=0)
    msr spsr_el1, x9
    eret

/* The first trap must be a real EL0 SVC64. Entry on SP_EL1. */
.balign 4
nas_arm_user_sync:
    mov x4, x0
    mov x3, x8
    mrs x2, spsr_el1
    mrs x1, elr_el1
    mrs x0, esr_el1
    bl nas_arm_user_trap_c
1:  b 1b
