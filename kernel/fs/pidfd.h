#ifndef NASEED_FS_PIDFD_H
#define NASEED_FS_PIDFD_H
#include <stdint.h>

/* Linux pidfd_open(434): process identity lives in one refcounted File
 * description, independently of the issuing task's FD table. */
uint64_t nas_pidfd_open(uint64_t pid,uint64_t flags);
uint16_t nas_pidfd_poll_fd(uint64_t fd,uint16_t events);
uint16_t nas_pidfd_poll_description(unsigned slot,uint16_t events);
uint64_t nas_pidfd_read(unsigned slot,uint8_t *dst,uint64_t count);
void nas_pidfd_release(unsigned slot);

#endif
