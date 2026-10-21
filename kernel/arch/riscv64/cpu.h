#ifndef NASEED_RISCV64_CPU_H
#define NASEED_RISCV64_CPU_H
#include <stdint.h>
#include <stdbool.h>
void nas_arch_halt(void) __attribute__((noreturn));
uint64_t nas_riscv_satp_read(void);
bool nas_riscv_validate_limine_mode(uint64_t mode);
unsigned nas_riscv_page_table_levels(uint64_t limine_mode);
#endif
