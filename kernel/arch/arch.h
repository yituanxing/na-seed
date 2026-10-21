#ifndef NASEED_ARCH_API_H
#define NASEED_ARCH_API_H
#if defined(__x86_64__)
#include "x86_64/cpu.h"
#elif defined(__riscv) && __riscv_xlen == 64
#include "riscv64/cpu.h"
#elif defined(__aarch64__)
#include "aarch64/cpu.h"
#else
#error "NaSeed architecture backend not implemented"
#endif
struct nas_task;
/* Shared mechanism API: these must be real, not silent stubs, before a
 * non-x86 full-kernel binary may link. */
void nas_arch_cpu_tables_init(void);
void nas_arch_cpu_user_fp_init(void);
void nas_arch_syscall_enable(void);
void nas_arch_enter_user(uint64_t entry, uint64_t sp);
uint64_t nas_arch_cycle_counter(void);
void nas_arch_set_user_tls(uint64_t);
/* True only when user mappings can enforce a non-executable protection. */
#if defined(__x86_64__)
static inline bool nas_arch_user_nx_supported(void) { return nas_arch_nx_enabled(); }
#else
bool nas_arch_user_nx_supported(void);
#endif
#if defined(__x86_64__)
extern uint64_t syscall_user_rsp;
#endif
void nas_arch_restore_cpu(const struct nas_task *);
void nas_arch_save_fpu(struct nas_task *);
void nas_arch_save_user_frame(struct nas_task *, const uint64_t *frame);
void nas_arch_set_saved_return(struct nas_task *, uint64_t value);
uint64_t nas_arch_saved_return(const struct nas_task *);
void nas_arch_load_user_frame(const struct nas_task *, uint64_t *frame);
#endif
