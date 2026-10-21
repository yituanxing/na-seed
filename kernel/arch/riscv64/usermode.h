#ifndef NASEED_RISCV64_USERMODE_H
#define NASEED_RISCV64_USERMODE_H
#include <stdint.h>
/* Minimal, *real* U-mode execution after the authenticated Limine M0 smoke.
 * This is an isolated machine trap test, not a complete task/syscall backend. */
void nas_rv_user_smoke(uint64_t limine_mode) __attribute__((noreturn));
void nas_rv_user_trap_c(uint64_t cause, uint64_t sstatus, uint64_t sepc,
                       uint64_t nr, uint64_t result) __attribute__((noreturn));
void nas_rv_jump_user(uint64_t satp, uint64_t pc, uint64_t user_sp,
                      uint64_t trap_sp) __attribute__((noreturn));
extern const unsigned char nas_rv_user_start[], nas_rv_user_end[];
#endif
