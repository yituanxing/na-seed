#ifndef NASEED_LINUX_VIRTGPU_VQ_BRIDGE_H
#define NASEED_LINUX_VIRTGPU_VQ_BRIDGE_H
/* Kernel-side ownership bridge for the unmodified Linux virtgpu_vq.c.
 * This is NOT a second VirtIO queue or a 3D command implementation.
 * The original Linux scatterlist/dma_map path will supply mapped segments.
 * Caller owns queue locking and payload DMA leases until used completion.
 */
#include "../unikraft/split_queue.h"

struct nas_linux_virtgpu_vq_bridge {
    struct nas_vring_queue *queue;
    void *cookies[NAS_VRING_MAX_DESC];
};
/* Bind only a newly initialized empty queue, before device-visible work. */
int nas_linux_virtgpu_vq_bind(struct nas_linux_virtgpu_vq_bridge *,
                             struct nas_vring_queue *);
/* Equivalent ownership semantics to Linux virtqueue_add_sgs AFTER SG mapping:
 * successful return publishes descriptors but never kicks the device. */
int nas_linux_virtgpu_vq_submit(struct nas_linux_virtgpu_vq_bridge *,
                               const struct nas_vring_segment *,
                               unsigned count, void *cookie);
/* Linux virtqueue_get_buf semantics: completion length and ORIGINAL cookie,
 * and -EAGAIN if the device has not completed anything. */
int nas_linux_virtgpu_vq_reclaim(struct nas_linux_virtgpu_vq_bridge *,
                                void **cookie, uint32_t *used_bytes);
bool nas_linux_virtgpu_vq_kick_prepare(struct nas_linux_virtgpu_vq_bridge *);
void nas_linux_virtgpu_vq_notify(struct nas_linux_virtgpu_vq_bridge *);
#endif
