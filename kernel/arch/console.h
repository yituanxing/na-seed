#ifndef NASEED_ARCH_CONSOLE_H
#define NASEED_ARCH_CONSOLE_H
/* Platform-specific byte transport. The user of these functions is the
 * shared drivers/serial.c formatter, never FS/process/VM directly. */
void nas_arch_console_init(void);
void nas_arch_console_putc(char c);
#endif
