#ifndef NASEED_ARCH_X86_CONTEXT_H
#define NASEED_ARCH_X86_CONTEXT_H
#include <stdint.h>
struct nas_arch_context {
    uint64_t resume_regs[15];
    uint64_t resume_user_rsp;
    /* CPU interrupt return state: SYSRET cannot preserve arbitrary RCX/R11.
     * All userspace returns use an IRET frame once timer IRQs are enabled. */
    uint64_t resume_user_rip, resume_user_rflags;
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    uint64_t user_fs_base;
};
#endif
