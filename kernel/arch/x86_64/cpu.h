#ifndef NASEED_ARCH_X86_CPU_H
#define NASEED_ARCH_X86_CPU_H
#include <stdint.h>
#include <stdbool.h>
void nas_arch_outb(uint16_t, uint8_t);
void nas_arch_cpu_tables_init(void);
void nas_arch_cpu_user_fp_init(void);
void nas_arch_syscall_enable(void);
void nas_arch_enter_user(uint64_t, uint64_t);
void nas_arch_halt(void) __attribute__((noreturn));
uint64_t nas_arch_current_page_root(void);
void nas_arch_switch_page_root(uint64_t);
bool nas_arch_la57_enabled(void);
void nas_arch_invalidate_page(uint64_t);
uint64_t nas_arch_cycle_counter(void);
bool nas_arch_nx_enabled(void);
void nas_arch_set_user_tls(uint64_t);
#endif
