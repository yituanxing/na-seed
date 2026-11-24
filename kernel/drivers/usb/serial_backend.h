#ifndef NASEED_USB_SERIAL_BACKEND_H
#define NASEED_USB_SERIAL_BACKEND_H
#include <stdbool.h>
#include <stdint.h>

/* Borrowed USB serial class backend; no Linux FD or QEMU dependency.
 * The existing chrdev registry owns publication, not this interface.
 * A stable driver holds this object, the class pins it across publication
 * and open File lifetimes, and hardware shutdown refuses a live pin.
 * Controller DMA/IRQ/BAR quiescence remains driver-owned.
 * This is a bounded single-CPU contract, not yet arbitrary USB hotplug. */
struct nas_usb_serial_backend {
    void *context;
    bool (*ready)(void *);
    int (*publication_pin)(void *);
    void (*publication_unpin)(void *);
    unsigned (*rx_available)(void *);
    void (*flush_rx)(void *);
    int (*read)(void *,uint8_t *,uint32_t);
    int (*write)(void *,const uint8_t *,uint32_t);
};
#endif
