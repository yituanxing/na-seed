#include <stdint.h>
#include "serial.h"
#include "../arch/console.h"
/* One console formatting API across all ISAs; the platform implements only
 * device initialization and one byte of output. No duplicate architecture
 * decimal, hex or string printers. */
void nas_serial_init(void) { nas_arch_console_init(); }
void nas_putchar(char ch) { nas_arch_console_putc(ch); }
void nas_print(const char *s) {
    if (!s) return;
    while (*s) nas_putchar(*s++);
}
void nas_dec(uint64_t n) {
    char digits[21]; unsigned len = 0;
    do { digits[len++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (len) nas_putchar(digits[--len]);
}
void nas_hex(uint64_t n) {
    static const char digits[] = "0123456789abcdef";
    nas_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        nas_putchar(digits[(n >> shift) & 15]);
}
