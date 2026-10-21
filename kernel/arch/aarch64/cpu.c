#include "cpu.h"

/* Limine AArch64 enters EL1 with the MMU enabled. This first port checks
 * the actual CPU state, rather than importing the x86 CR3 or RISC-V SATP
 * contract. TCR_EL1.TG1 == 0b10 selects a 4KiB TTBR1 kernel granule. */
uint64_t nas_arm64_read_tcr(void) {
    uint64_t tcr;
    __asm__ volatile ("mrs %0, tcr_el1" : "=r"(tcr));
    return tcr;
}
bool nas_arm64_el1_4k_mmu_active(void) {
    uint64_t level, sctlr;
    __asm__ volatile ("mrs %0, CurrentEL" : "=r"(level));
    if (level != 4) return false; /* CurrentEL[3:2] == 1 */
    __asm__ volatile ("mrs %0, sctlr_el1" : "=r"(sctlr));
    uint64_t tcr = nas_arm64_read_tcr();
    return (sctlr & 1) && (((tcr >> 30) & 3) == 2);
}
/* AArch64 TTBR0 user regime from TCR_EL1.T0SZ; reject unsupported
 * 4KiB user range instead of assuming a fixed x86 USER_LIMIT. */
uint64_t nas_arch_user_limit(void) {
    unsigned t0sz = (unsigned)(nas_arm64_read_tcr() & 63U);
    return (t0sz >= 16 && t0sz <= 25) ? (1ULL << (64 - t0sz)) : 0;
}
bool nas_arch_user_nx_supported(void) {
    return nas_arm64_el1_4k_mmu_active(); /* EL0 UXN descriptor enforcement */
}
void nas_arch_halt(void) {
    for (;;) __asm__ volatile ("wfi");
}

#ifndef NASEED_PORT_M0
#include "../../task.h"
#include "../context.h"
/* Use Limine's EL1 boot stack, but fix its SPSel=0 setting BEFORE
 * installing a userspace stack in SP_EL0. Otherwise the kernel's C stack
 * instantly switches to the user stack and faults during ERET preparation.
 * SP_EL1 is set to the SAME numeric boot stack while live C frames exist.
 * As on RV64, user exception handling remains syscall-boundary cooperative.
 */
void nas_arch_prepare_user_cpu(void) {
    uint64_t el, spsel;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    __asm__ volatile("mrs %0, SPSel" : "=r"(spsel));
    if (el != 4) nas_arch_halt();
    if (!(spsel & 1U)) {
        uint64_t current_sp;
        __asm__ volatile("mov %0, sp" : "=r"(current_sp));
        __asm__ volatile("msr sp_el1,%0\nmsr spsel,#1\nisb"
                         : : "r"(current_sp) : "memory");
    }
    __asm__ volatile("msr daifset,#15" : : : "memory");
    /* Kernel save/restore uses genuine NEON/FP state. */
    uint64_t cpacr;
    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ULL<<20); /* FPEN 11: EL0/EL1 FP access */
    __asm__ volatile("msr cpacr_el1,%0\nisb"
                     : : "r"(cpacr) : "memory");
    /* Stage-1 user leaf AttrIndx=7. Install normal write-back memory
     * there without altering Limine's existing kernel indices 0..6. */
    uint64_t mair;
    __asm__ volatile("mrs %0, mair_el1" : "=r"(mair));
    mair = (mair & ~(255ULL<<56)) | (255ULL<<56);
    __asm__ volatile("msr mair_el1,%0\nisb"
                     : : "r"(mair) : "memory");
    /* TTBR0 lower EL0: 39-bit 3-level, 4KiB, inner shareable, WB WA.
     * Leave the HIGH TTBR1 kernel translation regime unchanged. */
    uint64_t tcr=nas_arm64_read_tcr();
    tcr &= ~((63ULL<<0)|(3ULL<<8)|(3ULL<<10)|
             (3ULL<<12)|(3ULL<<14)|(1ULL<<7));
    tcr |= 25ULL | (1ULL<<8) | (1ULL<<10) | (3ULL<<12);
    __asm__ volatile("msr tcr_el1,%0\nisb" : : "r"(tcr) : "memory");
}
extern void nas_arm_full_vectors(void);
void nas_arch_enable_user_traps(void) {
    uint64_t vbar=(uint64_t)(uintptr_t)nas_arm_full_vectors;
    __asm__ volatile("msr vbar_el1,%0\nisb"
                     : : "r"(vbar) : "memory");
}
uint64_t nas_arch_cycle_counter(void) {
    uint64_t count;
    __asm__ volatile("mrs %0,cntvct_el0" : "=r"(count));
    return count;
}
void nas_arch_set_user_tls(uint64_t addr) {
    if(nas_current_task)
        nas_arch_context_set_tls(&nas_current_task->arch,addr);
    __asm__ volatile("msr tpidr_el0,%0" : : "r"(addr) : "memory");
}
#endif
