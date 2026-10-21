#include <stdbool.h>
#include <stdint.h>
#include "../../drivers/serial.h"
/* Supervisor-mode console: probe SBI DBCN first, legacy_putchar fallback.
 * No guesswork about UART MMIO mapping in the Limine HHDM. */
struct sbi_ret { long error, value; };
static struct sbi_ret call_sbi(long eid, long fid, long byte) {
    register long a0 __asm__("a0") = byte;
    register long a1 __asm__("a1") = 0;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;
    __asm__ volatile ("ecall" : "+r"(a0), "+r"(a1)
                      : "r"(a6), "r"(a7) : "memory");
    return (struct sbi_ret){a0, a1};
}
static bool use_dbcn;
void nas_serial_init(void) {
    struct sbi_ret r = call_sbi(0x10, 3, 0x4442434e);
    use_dbcn = r.error == 0 && r.value != 0;
}
void nas_putchar(char c) {
    if (use_dbcn) {
        struct sbi_ret r = call_sbi(0x4442434e, 2, (unsigned char)c);
        if (r.error == 0) return;
    }
    (void)call_sbi(1, 0, (unsigned char)c);
}
void nas_print(const char *s) { while (*s) nas_putchar(*s++); }
void nas_dec(uint64_t n) {
    char out[21]; unsigned i = 0;
    do { out[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (i) nas_putchar(out[--i]);
}
void nas_hex(uint64_t n) {
    static const char d[] = "0123456789abcdef";
    nas_print("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        nas_putchar(d[(n >> shift) & 15]);
}
