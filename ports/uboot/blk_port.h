#ifndef NAS_UBOOT_BLOCK_PORT_H
#define NAS_UBOOT_BLOCK_PORT_H
/* The original U-Boot virtio_blk.c is compiled unchanged in blk_port.c.
 * The compatibility facade is device-model plumbing, not a new block driver.
 */
#include "../../kernel/drivers/port/block.h"
#include "../../kernel/drivers/virtio/transport.h"
#include "../unikraft/split_queue.h"
#include "compat/virtio.h"
#include "compat/virtio_ring.h"
struct virtqueue{
 struct nas_vring_queue *ring;
 struct nas_virtio_transport *transport;
 struct nas_dma_page *header,*payload;
 struct virtio_sg *sg_out;
 struct virtio_sg *sg_in;
 unsigned in_count,out_count;
 uint16_t pending_head;
 bool pending,poisoned;
};
struct nas_uboot_block{
 struct nas_port_block port;
 struct nas_virtio_transport transport;
 struct nas_vring_queue ring;
 struct nas_dma_page header_dma,payload_dma;
 struct virtio_dev_priv parent_priv;
 struct udevice parent,udev;
 struct blk_desc desc;
 struct virtqueue vq;
 bool busy,poisoned;
 bool flush_supported;
};
int nas_uboot_block_attach(struct nas_uboot_block *,
 const struct nas_virtio_transport *,const char *name);
int nas_uboot_block_detach(struct nas_uboot_block *);
#endif
