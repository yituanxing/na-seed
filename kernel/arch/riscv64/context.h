#ifndef NASEED_RISCV64_CONTEXT_H
#define NASEED_RISCV64_CONTEXT_H
#include <stdint.h>
/* Linux RV64 hardware U-mode trap snapshot. All 31 GPRs, user SP, sepc,
 * sstatus and hard-float state are preserved across cooperative fork/pipe
 * scheduling. The shared process/scheduler code never knows this layout. */
struct nas_rv_trap_frame {
    uint64_t x[32];
    uint64_t sepc, sstatus;
};
_Static_assert(sizeof(struct nas_rv_trap_frame)==272,"RV64 trap frame ABI");
struct nas_arch_context {
    uint64_t user_regs[32];
    uint64_t user_pc, user_sp, user_status;
    uint64_t user_tls_base;
    uint64_t user_satp;
    uint64_t fp_regs[32];
    uint64_t fp_fcsr;
};
void nas_rv_full_trap_c(struct nas_rv_trap_frame *frame);
#endif
