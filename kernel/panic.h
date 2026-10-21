#ifndef NASEED_PANIC_H
#define NASEED_PANIC_H
#include "arch/arch.h"
#include "drivers/serial.h"
static inline void nas_fail(const char *msg) __attribute__((noreturn));
static inline void nas_fail(const char *msg) {
    nas_print("[FAIL] M1 ");
    nas_print(msg);
    nas_print("\n");
    nas_arch_halt();
}
#endif
