#ifndef NASEED_VIRTIO_INPUT_H
#define NASEED_VIRTIO_INPUT_H

/* One class driver for VirtIO input over both PCI and MMIO transports.
 * Polling is a transport-neutral receive completion service: an eventual
 * IRQ handler can schedule the same drain operation without changing input
 * core or evdev.
 */
int nas_virtio_input_driver_register(void);
unsigned nas_virtio_input_count(void);
int nas_virtio_input_poll(unsigned budget);

#endif
