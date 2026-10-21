#ifndef NASEED_ARCH_API_H
#define NASEED_ARCH_API_H
#if defined(__x86_64__)
#include "x86_64/cpu.h"
#else
#error "NaSeed architecture backend not implemented"
#endif
struct nas_task;
extern uint64_t syscall_user_rsp;
void nas_arch_restore_cpu(const struct nas_task *);
void nas_arch_save_fpu(struct nas_task *);
#endif
