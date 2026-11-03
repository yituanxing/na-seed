#ifndef NASEED_VIRTIO_INPUT_H
#define NASEED_VIRTIO_INPUT_H

/* One class driver for VirtIO input over both PCI and MMIO transports.
 * Routed native devices are IRQ-notified and drained at a safe scheduler
 * boundary. Route-less historical test buses retain explicit bounded poll. */
int nas_virtio_input_driver_register(void);
unsigned nas_virtio_input_count(void);
int nas_virtio_input_poll(unsigned budget);
#ifdef NAS_ENABLE_VIRTIO_INPUT
/* Emit one real-hardware, multi-device event/IRQ proof when satisfied. */
void nas_virtio_input_irq_report(void);
#endif

#endif
