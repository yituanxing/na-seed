#ifndef NASEED_INPUT_CHRDEV_H
#define NASEED_INPUT_CHRDEV_H
#include <stdint.h>
/* Register the evdev character-device handler before VirtIO bus discovery. */
int nas_input_chrdev_init(void);
uint64_t nas_input_chrdev_read_plan(unsigned slot,uint64_t requested);
uint64_t nas_input_chrdev_read(unsigned slot,uint8_t *dst,uint64_t n);
uint16_t nas_input_chrdev_poll(unsigned slot,uint16_t events);
uint64_t nas_input_chrdev_ioctl(uint64_t fd,uint64_t cmd,uint64_t arg);
void nas_input_chrdev_release(unsigned slot);
#endif
