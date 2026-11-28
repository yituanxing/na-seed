#ifndef NASEED_RISCV64_CPU_H
#define NASEED_RISCV64_CPU_H
#include <stdint.h>
#include <stdbool.h>
void nas_arch_halt(void) __attribute__((noreturn));
uint64_t nas_riscv_satp_read(void);
#endif
