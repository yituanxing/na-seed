#ifndef NAS_UBOOT_RING_H
#define NAS_UBOOT_RING_H
#include "virtio.h"
struct virtio_sg{void *addr;uint32_t length;};
struct virtqueue;
int virtqueue_add(struct virtqueue *,struct virtio_sg **,unsigned,unsigned);
void virtqueue_kick(struct virtqueue *);
void *virtqueue_get_buf(struct virtqueue *,unsigned *);
#endif
