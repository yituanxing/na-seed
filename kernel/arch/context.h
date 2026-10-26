#ifndef NASEED_CONTEXT_SELECT_H
#define NASEED_CONTEXT_SELECT_H
#include <stdint.h>
#if defined(__x86_64__)
#include "x86_64/context.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "riscv64/context.h"
#elif defined(__aarch64__)
#include "aarch64/context.h"
#else
#error "NaSeed task context backend not implemented"
#endif
/* Task lifecycle cannot assume FSBASE is how every ISA represents TLS. */
static inline uint64_t nas_arch_context_tls_base(const struct nas_arch_context *c) {
#if defined(__x86_64__)
    return c->user_fs_base;
#else
    return c->user_tls_base;
#endif
}
static inline void nas_arch_context_set_tls(struct nas_arch_context *c, uint64_t value) {
#if defined(__x86_64__)
    c->user_fs_base = value;
#else
    c->user_tls_base = value;
#endif
}
/* Linux clone() may supply an alternate userspace stack. Keep Task generic. */
static inline void nas_arch_context_set_user_sp(struct nas_arch_context *c,
                                                 uint64_t sp) {
#if defined(__x86_64__)
    c->resume_user_rsp = sp;
#elif defined(__riscv) && __riscv_xlen == 64
    c->user_sp = sp;
    c->user_regs[2] = sp;
#elif defined(__aarch64__)
    c->user_sp = sp;
#endif
}
/* ptrace observes an actual pre-syscall stop. This kernel cannot park a
 * live C continuation, so resume re-executes the identical user SYSCALL,
 * ECALL or SVC instruction once, using a per-Task replay guard. */
/* Construct true ELF entry registers for ptrace-stopped execve. */
static inline void nas_arch_context_exec(struct nas_arch_context *c,
                                          uint64_t entry,uint64_t sp) {
#if defined(__x86_64__)
    for(unsigned i=0;i<15;i++)c->resume_regs[i]=0;
    c->resume_user_rsp=sp;
    c->resume_user_rip=entry;
    c->resume_user_rflags=0x202;
    c->user_fs_base=0;
#elif defined(__riscv) && __riscv_xlen == 64
    for(unsigned i=0;i<32;i++)c->user_regs[i]=0;
    c->user_sp=sp;c->user_regs[2]=sp;
    c->user_pc=entry;
    c->user_status=(c->user_status & 0x6000ULL)|0x20ULL;
    c->user_tls_base=0;
#elif defined(__aarch64__)
    for(unsigned i=0;i<31;i++)c->user_regs[i]=0;
    c->user_sp=sp;c->user_elr=entry;
    c->user_spsr=0x340ULL;
    c->user_tls_base=0;
#endif
}
static inline void nas_arch_ptrace_rewind_syscall(struct nas_arch_context *c) {
#if defined(__x86_64__)
    c->resume_user_rip -= 2; /* syscall opcode 0F 05 */
#elif defined(__riscv) && __riscv_xlen == 64
    c->user_pc -= 4; /* ECALL always 32-bit */
#elif defined(__aarch64__)
    c->user_elr -= 4; /* SVC always 32-bit */
#endif
}
#endif
