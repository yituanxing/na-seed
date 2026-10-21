#ifndef NASEED_AARCH64_USERMODE_H
#define NASEED_AARCH64_USERMODE_H
#include <stdint.h>
/* First live TTBR0, EL0t + SVC #0 smoke. Not a general task engine. */
void nas_arm_user_smoke(void) __attribute__((noreturn));
void nas_arm_enter_el0(uint64_t root, uint64_t pc, uint64_t sp)
    __attribute__((noreturn));
extern const unsigned char nas_arm_user_code[], nas_arm_user_code_end[];
void nas_arm_user_trap_c(uint64_t esr, uint64_t elr, uint64_t spsr,
                         uint64_t nr, uint64_t arg0) __attribute__((noreturn));
void nas_arm_user_bad_vector(void) __attribute__((noreturn));
#endif
