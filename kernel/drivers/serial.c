#include <stdint.h>
#include "serial.h"
#include "../arch/console.h"
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
#include "display/fbcon.h"
#include <stdbool.h>
/* A diagnostic boot has no scheduler yet. This fixed buffer captures the
 * earliest Limine/PMM messages until the framebuffer has been validated. */
#define NAS_H0_BOOT_LOG_BYTES 4096U
static uint8_t h0_boot_log[NAS_H0_BOOT_LOG_BYTES];
static unsigned h0_boot_log_length;
static bool h0_framebuffer_active;
void nas_h0_serial_enable_framebuffer(void) {
    nas_fbcon_activate(true);
    if (h0_boot_log_length &&
        nas_fbcon_write(h0_boot_log,h0_boot_log_length)!=h0_boot_log_length)
        return;
    h0_framebuffer_active=true;
}
#endif
/* One console formatting API across all ISAs; the platform implements only
 * device initialization and one byte of output. No duplicate architecture
 * decimal, hex or string printers. */
void nas_serial_init(void) { nas_arch_console_init(); }
void nas_putchar(char ch) {
    nas_arch_console_putc(ch);
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
    if (h0_framebuffer_active)
        (void)nas_fbcon_write((const uint8_t *)&ch,1U);
    else if (h0_boot_log_length<NAS_H0_BOOT_LOG_BYTES)
        h0_boot_log[h0_boot_log_length++]=(uint8_t)ch;
#endif
}
void nas_print(const char *s) {
    if (!s) return;
    while (*s) {
        /* Diagnostic terminal formatting is separate from raw stdout. */
        if (*s == '\n') nas_arch_console_log_newline();
        nas_putchar(*s++);
    }
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
