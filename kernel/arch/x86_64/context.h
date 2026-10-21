#ifndef NASEED_ARCH_X86_CONTEXT_H
#define NASEED_ARCH_X86_CONTEXT_H
#include <stdint.h>
struct nas_arch_context {
    uint64_t resume_regs[15];
    uint64_t resume_user_rsp;
    uint8_t fpu_state[512] __attribute__((aligned(16)));
    uint64_t user_fs_base;
};
#endif
