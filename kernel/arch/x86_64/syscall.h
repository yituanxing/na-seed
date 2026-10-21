#ifndef NASEED_X86_SYSCALL_H
#define NASEED_X86_SYSCALL_H
#include <stdint.h>
/* Linux x86-64 arch_prctl ABI, not a generic task syscall. */
uint64_t nas_arch_prctl(uint64_t op, uint64_t address);
#endif
