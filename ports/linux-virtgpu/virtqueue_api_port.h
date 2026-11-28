#ifndef NASEED_LINUX_VIRTGPU_VIRTQUEUE_API_PORT_H
#define NASEED_LINUX_VIRTGPU_VIRTQUEUE_API_PORT_H
#include <linux/virtio.h>
#include "virtqueue_bridge.h"
/* A single real NaSeed split queue backs a Linux-visible virtqueue.
 * Runtime owner must serialize queue operations and stop DMA before closing.
 * No auto-release of GEM backing pages on a USED completion. */
struct nas_linux_virtgpu_virtqueue_port {
    struct virtqueue linux_vq;
    struct nas_linux_virtgpu_vq_bridge bridge;
};
int nas_linux_virtgpu_virtqueue_bind(
    struct nas_linux_virtgpu_virtqueue_port *,
    struct nas_vring_queue *,struct virtio_device *,unsigned index);
#endif
