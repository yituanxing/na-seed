#include <stdint.h>
#include "../console.h"
/* x86 PC 16550 COM1 port IO: hardware backend only. */
#define COM1 0x3f8
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
void nas_arch_console_init(void) {
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 1);
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 3);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}
static void raw_putc(char ch) {
    unsigned timeout = 1000000;
    while (timeout-- && !(inb(COM1 + 5) & 0x20)) {}
    if (inb(COM1 + 5) & 0x20) outb(COM1, (uint8_t)ch);
}
void nas_arch_console_putc(char ch) { raw_putc(ch); }
bool nas_arch_console_has_rx(void) { return true; }
bool nas_arch_console_getc(uint8_t *byte) {
    if(!(inb(COM1+5)&1U))return false;
    *byte=inb(COM1);return true;
}
void nas_arch_console_log_newline(void) { raw_putc('\r'); }
