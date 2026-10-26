#ifndef NASEED_TASK_PTRACE_H
#define NASEED_TASK_PTRACE_H
#include <stdint.h>
/* A Linux native request decoder at the shared Task owner. This subset
 * registers a real tracer and allows genuine SIGSTOP continue/detach.
 * PTRACE_SYSCALL/GETREGSET require true event/arch providers, so they fail
 * explicitly rather than manufacturing a successful trace. */
uint64_t nas_ptrace_request(uint64_t request,uint64_t pid,
                             uint64_t addr,uint64_t data);
#endif
