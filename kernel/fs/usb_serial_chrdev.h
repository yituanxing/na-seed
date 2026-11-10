#ifndef NASEED_USB_SERIAL_CHRDEV_H
#define NASEED_USB_SERIAL_CHRDEV_H
#include <stdint.h>

int nas_usb_serial_chrdev_publish(void);
int nas_usb_serial_chrdev_unpublish(void);
uint64_t nas_usb_serial_chrdev_read_plan(unsigned slot,uint64_t requested);
uint64_t nas_usb_serial_chrdev_read(unsigned slot,uint8_t *dst,uint64_t n);
uint64_t nas_usb_serial_chrdev_write(unsigned slot,const uint8_t *src,uint64_t n);
uint16_t nas_usb_serial_chrdev_poll(unsigned slot,uint16_t events);
uint64_t nas_usb_serial_chrdev_ioctl(uint64_t fd,uint64_t cmd,uint64_t arg);
void nas_usb_serial_chrdev_release(unsigned slot);
#endif
