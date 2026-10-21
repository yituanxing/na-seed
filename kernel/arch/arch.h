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
extern uint64_t syscall_user_rsp;
void nas_arch_restore_cpu(const struct nas_task *);
void nas_arch_save_fpu(struct nas_task *);
void nas_arch_save_user_frame(struct nas_task *, const uint64_t *frame);
void nas_arch_set_saved_return(struct nas_task *, uint64_t value);
uint64_t nas_arch_saved_return(const struct nas_task *);
void nas_arch_load_user_frame(const struct nas_task *, uint64_t *frame);
#endif
