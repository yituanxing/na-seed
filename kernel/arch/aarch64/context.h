#ifndef NASEED_AARCH64_CONTEXT_H
#define NASEED_AARCH64_CONTEXT_H
#include <stdint.h>
/* AArch64 EL0 saved-state shape; SVC frame and task switch must implement
 * it before the full shared kernel is allowed to link/boot. */
struct nas_arch_context {
    uint64_t user_regs[31];
    uint64_t user_sp, user_elr, user_spsr;
    uint64_t user_tls_base;
    uint64_t user_ttbr0;
};
#endif
