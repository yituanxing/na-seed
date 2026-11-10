#ifndef NASEED_SERIAL_H
#define NASEED_SERIAL_H
#include <stdint.h>
void nas_serial_init(void);
void nas_print(const char *);
void nas_putchar(char);
void nas_dec(uint64_t);
void nas_hex(uint64_t);
#ifdef NAS_ENABLE_H0_HARDWARE_DISCOVERY
/* H0 only: mirror early serial diagnostics onto an initialized Limine GOP
 * framebuffer, retaining pre-framebuffer boot text for replay. */
void nas_h0_serial_enable_framebuffer(void);
#endif
#endif
