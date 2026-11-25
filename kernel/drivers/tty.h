#ifndef NASEED_TTY_H
#define NASEED_TTY_H
#include <stdint.h>
#include <stdbool.h>
struct nas_task;
struct nas_termios { uint32_t iflag,oflag,cflag,lflag; uint8_t line,cc[19]; };
/* One source of truth for native Linux N_TTY defaults. The caller supplies
 * its actual hardware c_cflag: the USB-serial FTDI link is 115200 8N1,
 * whereas the serial/PTY/virtual console use the historical B38400 mode.
 * Linux termios padding and unused control characters must remain zero. */
#define NAS_TERMIOS_CONSOLE_CFLAG (0000060U|0000200U|0004000U|0000017U)
static inline void nas_termios_defaults(struct nas_termios *t,uint32_t cflag) {
    *t=(struct nas_termios){
        .iflag=0000400U, /* ICRNL */
        .oflag=0000001U|0000004U, /* OPOST | ONLCR */
        .cflag=cflag,
        .lflag=0000001U|0000002U|0000010U, /* ISIG | ICANON | ECHO */
        .cc={[0]=3,[1]=28,[2]=127,[3]=21,[4]=4,[6]=1,[10]=26,[14]=23}
    };
}
_Static_assert(sizeof(struct nas_termios)==36,"native Linux TCGETS size");
void nas_tty_init(void);
void nas_tty_session_leader_exit(uint64_t sid);
/* Publish TTY class endpoints independently of the VFS namei path. */
int nas_tty_chrdev_init(void);
bool nas_tty_controlling(void);
bool nas_tty_enabled(void);
/* TTY access policy for explicit /dev/tty open-file descriptions only;
 * preserve the diagnostic raw boot serial contract. */
bool nas_tty_background_fd(uint64_t fd);
bool nas_tty_tostop(void);
bool nas_tty_onlcr(void);
void nas_tty_write_char(uint8_t byte);
/* Hardware pump -> line discipline -> task wake predicate (no usercopy).
 * Input is still polled on a real serial device until IRQ support lands. */
void nas_tty_pump(void);
bool nas_tty_read_waitable(void);
void nas_tty_blocking_read_start(struct nas_task *task);
bool nas_tty_read_ready(struct nas_task *task,uint64_t length);
bool nas_tty_poll_readable(void);
unsigned nas_tty_take_signal(uint64_t *sid,uint64_t *pgid);
void nas_tty_signal_unsupported(void);
uint64_t nas_tty_ioctl(uint64_t cmd,uint64_t arg);
/* Receive only into kernel memory; caller prepares the userspace destination
 * before the line discipline consumes any queued input. */
/* One logical read/readv request is admitted against the ORIGINAL length.
 * In particular, noncanonical VMIN must not be recalculated per iovec. */
uint64_t nas_tty_read_plan(uint64_t requested,bool nonblock);
uint64_t nas_tty_read(uint8_t *kernel_dst,uint64_t len,bool nonblock);
#endif
