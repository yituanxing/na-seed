#ifndef NASEED_VIRTIO_GPU_DISPLAY_H
#define NASEED_VIRTIO_GPU_DISPLAY_H

#include <stdint.h>

/* One architecture-neutral VirtIO GPU 2D class provider. The generic bus
 * resolves PCI/MMIO resources; this driver owns only the display resource,
 * its DMA backing, control queue and one IRQ action. */
int nas_virtio_gpu_driver_register(void);
unsigned nas_virtio_gpu_count(void);
void nas_virtio_gpu_report(void);

struct nas_virtio_gpu_stats {
    uint64_t submitted, completed, transfers, flushes, irq_acks;
    uint64_t backing_pages;
    uint32_t scanout_id, width, height;
};
void nas_virtio_gpu_get_stats(struct nas_virtio_gpu_stats *);

#endif
