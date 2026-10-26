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
/* Each ISA prepares its own exception/FPU and syscall entry protocol.
 * Shared init never calls x86 GDT/IDT/TSS/MSR functions by name.
 * RV/ARM full-kernel providers must be implemented before linking.
 */
void nas_arch_prepare_user_cpu(void);
void nas_arch_enable_user_traps(void);
void nas_arch_enter_user(uint64_t entry, uint64_t sp);
uint64_t nas_arch_cycle_counter(void);
/* Monotonic nanoseconds, independent of interrupt delivery and user Tasks. */
uint64_t nas_arch_monotonic_ns(void);
/* Report real hardware resolution (rounded up). */
uint64_t nas_arch_clock_resolution_ns(void);
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
/* Native Task scheduler restores the selected Task's trap-frame identity. */
void nas_arch_pin_live_trap(struct nas_task *);
void nas_arch_bind_live_trap(const struct nas_task *);
_Noreturn void nas_arch_resume_saved_user(void);
void nas_arch_save_fpu(struct nas_task *);
void nas_arch_save_user_frame(struct nas_task *, const uint64_t *frame);
/* Return the LIVE trap's userspace SP, never an ABI-normalized syscall
 * argument array passed to common dispatch. */
uint64_t nas_arch_signal_user_sp(void);
/* ISA-native Linux rt_sigframe provider. Every ISA implements the same
 * ownership boundary but NOT a shared opaque/restorer layout. */
bool nas_arch_signal_install_handler(uint64_t *user_return_frame,
                                      unsigned signum,const uint64_t action[4]);
uint64_t nas_arch_signal_sigreturn(uint64_t *syscall_frame);
bool nas_arch_signal_install_trampoline(void);
void nas_arch_set_saved_return(struct nas_task *, uint64_t value);
uint64_t nas_arch_saved_return(const struct nas_task *);
void nas_arch_load_user_frame(const struct nas_task *, uint64_t *frame);
#endif
