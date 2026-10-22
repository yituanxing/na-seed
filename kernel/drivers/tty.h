#ifndef NASEED_TTY_H
#define NASEED_TTY_H
#include <stdint.h>
#include <stdbool.h>
struct nas_termios { uint32_t iflag,oflag,cflag,lflag; uint8_t line,cc[19]; };
_Static_assert(sizeof(struct nas_termios)==36,"native Linux TCGETS size");
void nas_tty_init(void);
bool nas_tty_controlling(void);
bool nas_tty_enabled(void);
bool nas_tty_onlcr(void);
void nas_tty_write_char(uint8_t byte);
/* Hardware pump -> line discipline -> task wake predicate (no usercopy).
 * Input is still polled on a real serial device until IRQ support lands. */
void nas_tty_pump(void);
bool nas_tty_read_waitable(void);
bool nas_tty_read_ready(uint64_t length);
bool nas_tty_poll_readable(void);
unsigned nas_tty_take_signal(uint64_t *sid,uint64_t *pgid);
void nas_tty_signal_unsupported(void);
uint64_t nas_tty_ioctl(uint64_t cmd,uint64_t arg);
uint64_t nas_tty_read(uint64_t addr,uint64_t len,bool nonblock);
#endif
