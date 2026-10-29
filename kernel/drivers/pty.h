#ifndef NASEED_DRIVERS_PTY_H
#define NASEED_DRIVERS_PTY_H
#include <stdint.h>
#include <stdbool.h>
/* Open-description lifetime and terminal state are architecture-independent.
 * Device slots are shared by fork/dup; user pointers are touched only by the
 * current task through the syscall layer's file operations. */
#define NAS_PTY_LIMIT 8U
int nas_pty_new(unsigned slot);
int nas_pty_attach(unsigned number,unsigned slot);
int nas_pty_attach_controlling(unsigned slot);
int nas_pty_path_number(const char *path); /* -1 means not a /dev/pts/N */
bool nas_pty_present(unsigned number);
bool nas_pty_controlling(void);
unsigned nas_pty_number(unsigned slot);
void nas_pty_release(unsigned slot);
uint64_t nas_pty_read(uint64_t fd,uint64_t dst,uint64_t count);
uint64_t nas_pty_write(uint64_t fd,uint64_t src,uint64_t count);
uint64_t nas_pty_ioctl(uint64_t fd,uint64_t command,uint64_t arg);
uint16_t nas_pty_poll(unsigned slot,uint16_t events);
bool nas_pty_read_ready(unsigned slot);
bool nas_pty_write_ready(unsigned slot);
/* Deferred PTY ISIG notifications: consumed by scheduler, never delivered
 * while the master write() syscall still owns its current Task and FD. */
unsigned nas_pty_take_signal(uint64_t *sid,uint64_t *pgid);
#endif
