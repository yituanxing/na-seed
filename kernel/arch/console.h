#ifndef NASEED_ARCH_CONSOLE_H
#define NASEED_ARCH_CONSOLE_H
/* Platform-specific byte transport. The user of these functions is the
 * shared drivers/serial.c formatter, never FS/process/VM directly. */
#include <stdbool.h>
#include <stdint.h>
void nas_arch_console_init(void);
/* Late post-frame-allocation device RX init. Must not fake unmapped MMIO. */
bool nas_arch_console_enable_input(void);
void nas_arch_console_putc(char c);
#include <stdbool.h>
#include <stdint.h>
bool nas_arch_console_has_rx(void);
bool nas_arch_console_getc(uint8_t *byte);
/* Called only for diagnostic logs before a newline, not raw user stdout. */
void nas_arch_console_log_newline(void);
#endif
