#ifndef NASEED_FS_EVENTFD_H
#define NASEED_FS_EVENTFD_H
#include <stdint.h>
#include <stdbool.h>
uint64_t nas_eventfd_create(uint64_t initial,uint64_t flags);
/* Data-plane callbacks operate on a live shared open-file description and
 * kernel-owned bytes. A plan reports the eight-byte transfer size without
 * mutating counter state; the syscall edge owns all userspace copying. */
uint64_t nas_eventfd_read_plan(unsigned slot,uint64_t requested);
uint64_t nas_eventfd_write_plan(unsigned slot,uint64_t requested);
uint64_t nas_eventfd_read(unsigned slot,uint8_t *dst,uint64_t count);
uint64_t nas_eventfd_write(unsigned slot,const uint8_t *src,uint64_t count);
uint16_t nas_eventfd_poll(uint64_t fd,uint16_t events);
uint16_t nas_eventfd_poll_slot(unsigned slot,uint16_t events);
bool nas_eventfd_ready(unsigned slot,bool writing);
void nas_eventfd_release(unsigned slot);
#endif
