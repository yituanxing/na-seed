#ifndef NASEED_TASK_PTRACE_H
#define NASEED_TASK_PTRACE_H
#include <stdint.h>
/* Generic single-core ptrace owner. Task and signal/wait4 lifecycle is
 * common, native Linux register layout lives in arch/ptrace_regs.h. */
uint64_t nas_ptrace_request(uint64_t request,uint64_t pid,
                             uint64_t addr,uint64_t data);
struct nas_task;
bool nas_ptrace_syscall_enter(struct nas_task *task,const uint64_t *args);
bool nas_ptrace_syscall_exit(struct nas_task *task,const uint64_t *args,
                              uint64_t result);
#endif
