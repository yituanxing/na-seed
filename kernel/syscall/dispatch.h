#ifndef NASEED_SYSCALL_DISPATCH_H
#define NASEED_SYSCALL_DISPATCH_H
#include <stdint.h>
uint64_t syscall_dispatch(const uint64_t *saved_registers);
/* Actual ISA syscall gates use ptrace hooks around the shared dispatcher. */
uint64_t nas_ptrace_syscall_dispatch(uint64_t *saved_registers);
#endif
