#ifndef NASEED_RISCV64_CONTEXT_H
#define NASEED_RISCV64_CONTEXT_H
#include <stdint.h>
/* RV64 privileged trap/user frame data is architecture-owned; the current
 * one-shot ECALL smoke is NOT a task-switch implementation. This explicit
 * contract is intentionally not wired to a fake trap provider. */
struct nas_arch_context {
    uint64_t user_regs[32];
    uint64_t user_pc, user_sp, user_status;
    uint64_t user_tls_base;
    uint64_t user_satp;
};
#endif
