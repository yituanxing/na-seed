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
/* One Linux SG entry per mapped/borrowed page slice. It is NOT a raw
 * virtual-address-to-DMA shortcut: each map requires an explicit provider pin.
 * The mapped pages MUST remain active until the returned cookie is completed,
 * or after a verified device reset and abandoned-cookie recovery. */
struct nas_linux_virtgpu_dma_sg {
    const struct nas_dma_mapped_frame *map;
    uint32_t offset, length;
    bool device_writes;
};
int nas_linux_virtgpu_vq_submit_mapped(struct nas_linux_virtgpu_vq_bridge *,
                                      const struct nas_linux_virtgpu_dma_sg *,
                                      unsigned count, void *cookie);
/* Linux virtqueue_get_buf semantics: completion length and ORIGINAL cookie,
 * and -EAGAIN if the device has not completed anything. */
int nas_linux_virtgpu_vq_reclaim(struct nas_linux_virtgpu_vq_bridge *,
                                void **cookie, uint32_t *used_bytes);
/* Only AFTER nas_vring_queue_close() has reset the device, enumerate
 * abandoned DMA/request owners. A cookie must never be released before reset.
 * Set *cursor=0 initially; -ENOENT means no tokens remain. */
int nas_linux_virtgpu_vq_reap_after_reset(struct nas_linux_virtgpu_vq_bridge *,
                                         unsigned *cursor, void **cookie);
bool nas_linux_virtgpu_vq_kick_prepare(struct nas_linux_virtgpu_vq_bridge *);
void nas_linux_virtgpu_vq_notify(struct nas_linux_virtgpu_vq_bridge *);
void nas_linux_virtgpu_vq_disable_callbacks(struct nas_linux_virtgpu_vq_bridge *);
bool nas_linux_virtgpu_vq_enable_callbacks(struct nas_linux_virtgpu_vq_bridge *);
#endif
