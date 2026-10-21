#ifndef NASEED_SYSCALL_DISPATCH_H
#define NASEED_SYSCALL_DISPATCH_H
#include <stdint.h>
uint64_t syscall_dispatch(const uint64_t *saved_registers);
#endif
