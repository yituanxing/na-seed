/* SPDX-License-Identifier: GPL-2.0-or-later
 * ONE U-Boot device-model/VirtIO façade for pinned original blk and net
 * class drivers. This file contains no block protocol, Ethernet framing,
 * queue allocation, DMA or architecture-specific register operations. */
#include "compat/virtio.h"
#include "compat/virtio_ring.h"
#include "../../kernel/drivers/virtio/transport.h"
#include "../unikraft/split_queue.h"
#include "../../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>

static struct virtio_dev_priv *device_priv(struct udevice *d) {
    if(!d || !d->parent)return NULL;
    return (struct virtio_dev_priv *)dev_get_uclass_priv(d->parent);
}
void virtio_driver_features_init(struct virtio_dev_priv *p,
                                 const u32 *f,size_t count,
                                 const u32 *legacy,size_t legacy_count) {
    if(!p)return;
    const u32 *bits=p->legacy?legacy:f;
    size_t size=p->legacy?legacy_count:count;
    p->offered=0;
    if(!bits)return;
    for(size_t i=0;i<size;i++)if(bits[i]<64)
        p->offered|=UINT64_C(1)<<bits[i];
}
bool virtio_has_feature(struct udevice *d,unsigned n) {
    struct virtio_dev_priv *p=device_priv(d);
    return p && n<64 && (p->features & (UINT64_C(1)<<n))!=0;
}
void virtio_cread_bytes(struct udevice *d,size_t off,void *dst,size_t len) {
    struct virtio_dev_priv *p=device_priv(d);
    if(!p || !p->transport || !dst || off>UINT32_MAX || len>UINT32_MAX-off)
        return;
    uint8_t *bytes=dst;
    for(size_t i=0;i<len;i++){
        uint32_t address=(uint32_t)(off+i);
        uint32_t word=nas_virtio_config32(p->transport,address&~3U);
        bytes[i]=(uint8_t)(word>>((address&3U)*8U));
    }
}
void nas_uboot_virtio_cread(struct udevice *d,size_t off,size_t len,void *dst) {
    if(!dst || (len!=4U && len!=8U))return;
    virtio_cread_bytes(d,off,dst,len);
}
void virtio_cwrite8(struct udevice *d,size_t off,u8 value) {
    (void)off;(void)value;
    /* The active NaSeed transport has no device-config WRITE primitive.
     * All accepted devices are modern. Never fabricate legacy write success. */
    struct virtio_dev_priv *p=device_priv(d);
    if(p)nas_virtio_fail(p->transport);
}
int virtio_find_vqs(struct udevice *d,int count,struct virtqueue **out) {
    struct virtio_dev_priv *p=device_priv(d);
    if(!p || !out || count<=0 || (unsigned)count!=p->queue_count ||
       !p->queues)return -EINVAL;
    for(int i=0;i<count;i++)
        if(!p->queues[i] || !p->queues[i]->ring ||
           !p->queues[i]->ring->active)return -ENODEV;
    for(int i=0;i<count;i++)out[i]=p->queues[i];
    return 0;
}
int virtio_reset(struct udevice *d) {
    struct virtio_dev_priv *p=device_priv(d);
    if(!p || !p->transport || !p->transport->ops ||
       !p->transport->ops->reset)return -EINVAL;
    p->transport->ops->reset(p->transport->context);
    return 0;
}
int virtqueue_add(struct virtqueue *v,struct virtio_sg **sgs,
                  unsigned out,unsigned in) {
    return v && v->bridge && v->bridge->add ?
        v->bridge->add(v,sgs,out,in) : -ENODEV;
}
void virtqueue_kick(struct virtqueue *v) {
    if(v && v->bridge && v->bridge->kick)v->bridge->kick(v);
}
void *virtqueue_get_buf(struct virtqueue *v,unsigned *len) {
    return v && v->bridge && v->bridge->get_buf ?
        v->bridge->get_buf(v,len) : NULL;
}
