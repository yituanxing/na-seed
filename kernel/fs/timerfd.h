#ifndef NASEED_FS_TIMERFD_H
#define NASEED_FS_TIMERFD_H
#include <stdint.h>

/* timerfd state belongs to the shared OpenFile description, never an fd
 * number or a Task. These entry points are common to all Linux LP64 ISAs. */
uint64_t nas_timerfd_create(uint64_t clockid,uint64_t flags);
uint64_t nas_timerfd_settime(uint64_t fd,uint64_t flags,
                             uint64_t new_value,uint64_t old_value);
uint64_t nas_timerfd_gettime(uint64_t fd,uint64_t old_value);
uint64_t nas_timerfd_read_plan(unsigned slot,uint64_t count);
uint64_t nas_timerfd_read(unsigned slot,uint8_t *dst,uint64_t count);
uint16_t nas_timerfd_poll_slot(unsigned slot,uint16_t events);
uint16_t nas_timerfd_poll_fd(uint64_t fd,uint16_t events);
void nas_timerfd_release(unsigned slot);
#endif
