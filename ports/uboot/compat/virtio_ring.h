#ifndef NAS_UBOOT_RING_H
#define NAS_UBOOT_RING_H
#include "virtio.h"
struct virtio_sg{void *addr;uint32_t length;};
struct nas_vring_queue;
struct nas_dma_page;
struct virtqueue;
/* Exactly one U-Boot compatibility facade dispatches to class-owned queue
 * storage. Block and Ethernet use the SAME generic VirtIO transport and
 * split-ring algorithms; only SG staging and completion policy differ. */
struct nas_uboot_vq_ops {
 int (*add)(struct virtqueue *,struct virtio_sg **,unsigned,unsigned);
 void (*kick)(struct virtqueue *);
 void *(*get_buf)(struct virtqueue *,unsigned *);
};
struct virtqueue {
 const struct nas_uboot_vq_ops *bridge;
 void *owner;
 struct nas_vring_queue *ring;
 struct nas_virtio_transport *transport;
 struct nas_dma_page *header,*payload;
 struct virtio_sg *sg_out,*sg_in;
 unsigned in_count,out_count;
 uint16_t pending_head;
 bool pending,poisoned;
};
int virtqueue_add(struct virtqueue *,struct virtio_sg **,unsigned,unsigned);
void virtqueue_kick(struct virtqueue *);
void *virtqueue_get_buf(struct virtqueue *,unsigned *);
#endif
