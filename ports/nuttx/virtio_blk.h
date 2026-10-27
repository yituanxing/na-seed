#ifndef NASEED_NUTTX_VIRTIO_BLK_PORT_H
#define NASEED_NUTTX_VIRTIO_BLK_PORT_H
/* NaSeed platform port of the NuttX Apache-2.0 VirtIO-Blk request model.
 * Device is published through the sole NaSeed BlockDevice registry.
 * Caller-owned storage must be zero-initialized and lifetime stable.
 */
#include "../unikraft/split_queue.h"
#include "../../kernel/drivers/port/block.h"
struct nas_nuttx_virtio_blk {
    struct nas_port_block port;
    struct nas_virtio_transport transport;
    struct nas_vring_queue queue;
    struct nas_dma_page header_dma;
    struct nas_dma_page payload_dma;
    uint64_t capacity_512;
    uint32_t sector_bytes;
    bool supports_flush;
    bool poisoned;
    bool busy;
};
int nas_nuttx_virtio_blk_attach(struct nas_nuttx_virtio_blk *,
                                const struct nas_virtio_transport *,
                                const char *device_name);
int nas_nuttx_virtio_blk_detach(struct nas_nuttx_virtio_blk *);
#endif
