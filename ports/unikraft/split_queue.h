#ifndef NASEED_UNIKRAFT_SPLIT_QUEUE_H
#define NASEED_UNIKRAFT_SPLIT_QUEUE_H
/* NaSeed platform adapter around pinned upstream Unikraft vring layout.
 * This is transport/request independent and is NOT a VirtIO block driver.
 * One coherent DMA page contains all three rings; payload DMA mapping is
 * owned by the caller. Do not put ext4/block request protocol here.
 */
#include "../../kernel/drivers/virtio/transport.h"
#include <virtio/virtio_ring.h>
#include <stdint.h>
#include <stdbool.h>
#define NAS_VRING_MAX_DESC 128U
#define NAS_VRING_ALIGN 64U
struct nas_vring_segment {
    uint64_t bus_address;
    uint32_t length;
    bool device_writes;
};
struct nas_vring_queue {
    const struct nas_virtio_transport *transport;
    struct nas_dma_page dma_page;
    struct vring ring;
    uint16_t queue_id;
    uint16_t free_head;
    uint16_t free_count;
    uint16_t last_used;
    uint16_t inflight;
    uint8_t pending[NAS_VRING_MAX_DESC];
    bool active;
    bool poisoned;
};
int nas_vring_queue_init(struct nas_vring_queue *,
                         const struct nas_virtio_transport *,
                         unsigned queue_id, unsigned num_desc);
/* One call publishes the entire scatter/gather chain. A successful submit
 * never implies that the device completed the request. */
int nas_vring_queue_submit(struct nas_vring_queue *,
                           const struct nas_vring_segment *, unsigned count,
                           uint16_t *head);
/* -EAGAIN: no completion; 0: completed, descriptor chain reclaimed;
 * -EIO: corrupt completion, device marked failed. */
int nas_vring_queue_poll(struct nas_vring_queue *, uint16_t *head,
                         uint32_t *bytes);
/* Reset the transport before releasing a live queue, to stop device DMA. */
int nas_vring_queue_close(struct nas_vring_queue *);
#endif
