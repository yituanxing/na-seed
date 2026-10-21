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
void nas_arch_halt(void) {
    for (;;) __asm__ volatile ("wfi");
}
