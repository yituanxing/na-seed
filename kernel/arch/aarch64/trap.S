/* Live AArch64 EL0→EL1 SVC trap and common Task/VM entry.
 * The user app is a real ELF; syscall semantics are the shared dispatcher.
 * SP_EL1 must have been selected in nas_arch_prepare_user_cpu before
 * entering EL0; no independent ARM userland loop exists.
 */
.section .text,"ax",%progbits
/* Limine initializes EL1 with SPSel=0. Change stack banks AT BOOT ENTRY,
 * before any C prologue/stack frame exists. Otherwise the live kernel C
 * stack changes bank mid-call, which stalled both ARM EL0 attempts. */
.global nas_arm64_bootstrap
.balign 4
nas_arm64_bootstrap:
    /* Never depend on an uninitialized C stack/console to diagnose the
     * AArch64 ELF entry. QEMU's existing ARM semihosting SYS_WRITEC
     * transport prints 1-char A/B/C progression with no stack activity. */
    mov x0,#3
    adr x1,nas_arm_early_a
    hlt #0xf000
    mov x9,sp
    /* ARMv8-A: MSR SP_EL1, Xt is UNDEFINED when executed at EL1.
     * To initialize the EL1 stack from Limine's live SP_EL0, first
     * select SP_EL1 and then write through the architected SP alias.
     * The entire transition must happen before the FIRST C frame. */
    msr spsel,#1
    mov sp,x9
    isb
    mov x0,#3
    adr x1,nas_arm_early_b
    hlt #0xf000
    mov x0,#3
    adr x1,nas_arm_early_c
    hlt #0xf000
    b kmain
.section .rodata,"a",%progbits
nas_arm_early_a: .ascii "A"
nas_arm_early_b: .ascii "B"
nas_arm_early_c: .ascii "C"
.section .text,"ax",%progbits

.balign 2048
.global nas_arm_full_vectors
nas_arm_full_vectors:
.rept 8
    b nas_arm_unexpected_vector
    .balign 128
.endr
    b nas_arm_lower_sync
    .balign 128
.rept 7
    b nas_arm_unexpected_vector
    .balign 128
.endr

.global nas_arch_enter_user
.balign 4
nas_arch_enter_user:
    msr sp_el0,x1
    msr elr_el1,x0
    mov x2,#0x3c0       /* DAIF masked, EL0t */
    msr spsr_el1,x2
    msr tpidr_el0,xzr
    isb
    eret

.global nas_arm_lower_sync
.balign 4
nas_arm_lower_sync:
    sub sp,sp,#288
    stp x0,x1,[sp,#0]
    stp x2,x3,[sp,#16]
    stp x4,x5,[sp,#32]
    stp x6,x7,[sp,#48]
    stp x8,x9,[sp,#64]
    stp x10,x11,[sp,#80]
    stp x12,x13,[sp,#96]
    stp x14,x15,[sp,#112]
    stp x16,x17,[sp,#128]
    stp x18,x19,[sp,#144]
    stp x20,x21,[sp,#160]
    stp x22,x23,[sp,#176]
    stp x24,x25,[sp,#192]
    stp x26,x27,[sp,#208]
    stp x28,x29,[sp,#224]
    str x30,[sp,#240]
    mrs x0,sp_el0
    str x0,[sp,#248]
    mrs x0,elr_el1
    str x0,[sp,#256]
    mrs x0,spsr_el1
    str x0,[sp,#264]
    mrs x0,tpidr_el0
    str x0,[sp,#272]
    mov x0,sp
    bl nas_arm_full_trap_c
    ldr x9,[sp,#248]
    msr sp_el0,x9
    ldr x9,[sp,#256]
    msr elr_el1,x9
    ldr x9,[sp,#264]
    msr spsr_el1,x9
    ldr x9,[sp,#272]
    msr tpidr_el0,x9
    ldp x0,x1,[sp,#0]
    ldp x2,x3,[sp,#16]
    ldp x4,x5,[sp,#32]
    ldp x6,x7,[sp,#48]
    ldp x8,x9,[sp,#64]
    ldp x10,x11,[sp,#80]
    ldp x12,x13,[sp,#96]
    ldp x14,x15,[sp,#112]
    ldp x16,x17,[sp,#128]
    ldp x18,x19,[sp,#144]
    ldp x20,x21,[sp,#160]
    ldp x22,x23,[sp,#176]
    ldp x24,x25,[sp,#192]
    ldp x26,x27,[sp,#208]
    ldp x28,x29,[sp,#224]
    ldr x30,[sp,#240]
    add sp,sp,#288
    eret

nas_arm_unexpected_vector:
1:  wfi
    b 1b

.global nas_arm_save_simd
nas_arm_save_simd:
    str q0,[x0,#0]
    str q1,[x0,#16]
    str q2,[x0,#32]
    str q3,[x0,#48]
    str q4,[x0,#64]
    str q5,[x0,#80]
    str q6,[x0,#96]
    str q7,[x0,#112]
    str q8,[x0,#128]
    str q9,[x0,#144]
    str q10,[x0,#160]
    str q11,[x0,#176]
    str q12,[x0,#192]
    str q13,[x0,#208]
    str q14,[x0,#224]
    str q15,[x0,#240]
    str q16,[x0,#256]
    str q17,[x0,#272]
    str q18,[x0,#288]
    str q19,[x0,#304]
    str q20,[x0,#320]
    str q21,[x0,#336]
    str q22,[x0,#352]
    str q23,[x0,#368]
    str q24,[x0,#384]
    str q25,[x0,#400]
    str q26,[x0,#416]
    str q27,[x0,#432]
    str q28,[x0,#448]
    str q29,[x0,#464]
    str q30,[x0,#480]
    str q31,[x0,#496]
    mrs x1,fpcr
    str x1,[x0,#512]
    mrs x1,fpsr
    str x1,[x0,#520]
    ret

.global nas_arm_load_simd
nas_arm_load_simd:
    ldr q0,[x0,#0]
    ldr q1,[x0,#16]
    ldr q2,[x0,#32]
    ldr q3,[x0,#48]
    ldr q4,[x0,#64]
    ldr q5,[x0,#80]
    ldr q6,[x0,#96]
    ldr q7,[x0,#112]
    ldr q8,[x0,#128]
    ldr q9,[x0,#144]
    ldr q10,[x0,#160]
    ldr q11,[x0,#176]
    ldr q12,[x0,#192]
    ldr q13,[x0,#208]
    ldr q14,[x0,#224]
    ldr q15,[x0,#240]
    ldr q16,[x0,#256]
    ldr q17,[x0,#272]
    ldr q18,[x0,#288]
    ldr q19,[x0,#304]
    ldr q20,[x0,#320]
    ldr q21,[x0,#336]
    ldr q22,[x0,#352]
    ldr q23,[x0,#368]
    ldr q24,[x0,#384]
    ldr q25,[x0,#400]
    ldr q26,[x0,#416]
    ldr q27,[x0,#432]
    ldr q28,[x0,#448]
    ldr q29,[x0,#464]
    ldr q30,[x0,#480]
    ldr q31,[x0,#496]
    ldr x1,[x0,#512]
    msr fpcr,x1
    ldr x1,[x0,#520]
    msr fpsr,x1
    ret
