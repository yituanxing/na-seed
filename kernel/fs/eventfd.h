#ifndef NASEED_FS_EVENTFD_H
#define NASEED_FS_EVENTFD_H
#include <stdint.h>
#include <stdbool.h>
uint64_t nas_eventfd_create(uint64_t initial,uint64_t flags);
uint64_t nas_eventfd_read(uint64_t fd,uint64_t address,uint64_t count);
uint64_t nas_eventfd_write(uint64_t fd,uint64_t address,uint64_t count);
uint16_t nas_eventfd_poll(uint64_t fd,uint16_t events);
uint16_t nas_eventfd_poll_slot(unsigned slot,uint16_t events);
bool nas_eventfd_ready(unsigned slot,bool writing);
void nas_eventfd_release(unsigned slot);
#endif
