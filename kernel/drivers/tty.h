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
uint64_t nas_tty_ioctl(uint64_t cmd,uint64_t arg);
uint64_t nas_tty_read(uint64_t addr,uint64_t len,bool nonblock);
#endif
