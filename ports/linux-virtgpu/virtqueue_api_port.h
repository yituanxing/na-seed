#ifndef NASEED_LINUX_VIRTGPU_VIRTQUEUE_API_PORT_H
#define NASEED_LINUX_VIRTGPU_VIRTQUEUE_API_PORT_H
#include <linux/virtio.h>
#include "virtqueue_bridge.h"
/* The port's SG mapping registry is OUTSIDE Linux struct scatterlist.
 * All registrations are consumed by the next add_sgs call. This avoids
 * changing the upstream SG ABI and forbids accidental pointer->DMA guesses. */
struct nas_linux_virtgpu_sg_registration {
    const struct scatterlist *sg;
    const struct nas_dma_mapped_frame *map;
};
struct nas_linux_virtgpu_virtqueue_port {
    struct virtqueue linux_vq;
    struct nas_linux_virtgpu_vq_bridge bridge;
    struct nas_linux_virtgpu_sg_registration sg_maps[NAS_VRING_MAX_DESC];
    unsigned sg_count;
};
/* Bind one completely empty NaSeed ring, with direct descriptors only.
 * No indirect feature is advertised; upstream control-queue callers must
 * not treat a single indirect descriptor as available until implemented. */
int nas_linux_virtgpu_virtqueue_bind(
    struct nas_linux_virtgpu_virtqueue_port *,
    struct nas_vring_queue *,struct virtio_device *,unsigned index);
/* Explicit DMA mapping registration; caller retains its physical pin
 * until the command completes/fence retires, or verified device reset.
 * This is port-side mapping glue, not part of original Linux scatterlist. */
int nas_linux_virtgpu_sg_map(struct nas_linux_virtgpu_virtqueue_port *,
                            struct scatterlist *,
                            const struct nas_dma_mapped_frame *,
                            uint32_t offset,uint32_t length);
#endif
