#ifndef NASEED_SERIAL_H
#define NASEED_SERIAL_H
#include <stdint.h>
void nas_serial_init(void);
void nas_print(const char *);
void nas_putchar(char);
void nas_dec(uint64_t);
void nas_hex(uint64_t);
#endif
