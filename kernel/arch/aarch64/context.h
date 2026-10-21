#ifndef NASEED_AARCH64_CONTEXT_H
#define NASEED_AARCH64_CONTEXT_H
#include <stdint.h>
/* Saved native Linux EL0 GPR, PC, SP and TLS registers, independent of
 * common Task and ELF semantics. A live exception uses the exact same
 * layout plus hardware fields in nas_arm_trap_frame below. */
struct nas_arch_context {
    uint64_t user_regs[31];
    uint64_t user_sp, user_elr, user_spsr;
    uint64_t user_tls_base;
    uint64_t user_ttbr0;
    /* 32 x 128-bit SIMD regs and native FP control/status. */
    uint8_t simd_regs[512] __attribute__((aligned(16)));
    uint64_t fpcr, fpsr;
};
struct nas_arm_trap_frame {
    uint64_t x[31];
    uint64_t sp_el0, elr_el1, spsr_el1, tpidr_el0;
};
_Static_assert(sizeof(struct nas_arm_trap_frame)==280,"AArch64 trap ABI");
void nas_arm_full_trap_c(struct nas_arm_trap_frame *);
#endif
