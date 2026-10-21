#include <stdint.h>
#include "serial.h"
/* Genuine x86 PC 16550-compatible COM1, using bounded polling. */
#define COM1 0x3f8
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 1);      /* 115200 baud */
    outb(COM1 + 1, 0);
    outb(COM1 + 3, 3);      /* 8n1 */
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}
static void serial_putc(char ch) {
    unsigned timeout = 1000000;
    while (timeout-- && !(inb(COM1 + 5) & 0x20)) {}
    if (inb(COM1 + 5) & 0x20) outb(COM1, (uint8_t)ch);
}
static void serial_puts(const char *str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str++);
    }
}
static void serial_dec(uint64_t n) {
    char digits[21];
    unsigned used = 0;
    do {
        digits[used++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (used) serial_putc(digits[--used]);
}


void nas_serial_init(void) { serial_init(); }
void nas_print(const char *s) { serial_puts(s); }
void nas_putchar(char c) { serial_putc(c); }
void nas_dec(uint64_t v) { serial_dec(v); }
void nas_hex(uint64_t n) {
    static const char digits[] = "0123456789abcdef";
    serial_puts("0x");
    for (int bit = 60; bit >= 0; bit -= 4) serial_putc(digits[(n >> bit) & 15]);
}
