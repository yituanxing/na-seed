#include <stdbool.h>
#include <stdint.h>
#include "../console.h"
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
void nas_arch_console_init(void) {
    struct sbi_ret r = call_sbi(0x10, 3, 0x4442434e);
    use_dbcn = r.error == 0 && r.value != 0;
}
void nas_arch_console_putc(char c) {
    if (use_dbcn) {
        struct sbi_ret r = call_sbi(0x4442434e, 2, (unsigned char)c);
        if (r.error == 0) return;
    }
    (void)call_sbi(1, 0, (unsigned char)c);
}

/* SBI console handles plain LF; raw stdout bytes remain unmodified. */
void nas_arch_console_log_newline(void) { }
