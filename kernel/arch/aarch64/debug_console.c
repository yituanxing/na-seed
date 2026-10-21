#include <stdint.h>
#include "../console.h"

/* EARLY QEMU-ONLY diagnostic transport: AArch64 semihosting HLT 0xF000
 * with SYS_WRITEC. It is NOT a PL011 hardware driver or supported real
 * machine console; PL011 needs device discovery and valid MMIO mappings.
 * The CI QEMU command explicitly enables semihosting. */
void nas_arch_console_init(void) { }
void nas_arch_console_putc(char ch) {
    char ch_local = ch;
    register uint64_t x0 __asm__("x0") = 0x03; /* SYS_WRITEC */
    register uint64_t x1 __asm__("x1") = (uint64_t)(uintptr_t)&ch_local;
    __asm__ volatile ("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
}
