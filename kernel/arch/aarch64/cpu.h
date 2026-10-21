#ifndef NASEED_AARCH64_CPU_H
#define NASEED_AARCH64_CPU_H
#include <stdbool.h>
#include <stdint.h>
void nas_arch_halt(void) __attribute__((noreturn));
bool nas_arm64_el1_4k_mmu_active(void);
uint64_t nas_arm64_read_tcr(void);
#endif
