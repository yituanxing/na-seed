#include <stdint.h>
#include "../../drivers/serial.h"

/* EARLY QEMU-ONLY diagnostic transport: AArch64 semihosting HLT 0xF000
 * with SYS_WRITEC. It is NOT a PL011 hardware driver or supported real
 * machine console; PL011 needs device discovery and valid MMIO mappings.
 * The CI QEMU command explicitly enables semihosting. */
void nas_serial_init(void) { }
void nas_putchar(char ch) {
    char ch_local = ch;
    register uint64_t x0 __asm__("x0") = 0x03; /* SYS_WRITEC */
    register uint64_t x1 __asm__("x1") = (uint64_t)(uintptr_t)&ch_local;
    __asm__ volatile ("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
}
void nas_print(const char *s) { while (*s) nas_putchar(*s++); }
void nas_dec(uint64_t n) {
    char digits[21];
    unsigned len = 0;
    do { digits[len++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (len) nas_putchar(digits[--len]);
}
void nas_hex(uint64_t n) {
    static const char hex[] = "0123456789abcdef";
    nas_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        nas_putchar(hex[(n >> shift) & 0xf]);
}
