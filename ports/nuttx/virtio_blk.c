/* SPDX-License-Identifier: Apache-2.0
 * Port of the request flow from Apache NuttX drivers/virtio/virtio-blk.c
 * (Apache-2.0, ASF), pinned to 0884a521e587198fa303611c5f13a154b6f6d196.
 * Header/data/status ordering, sector scaling, read-only and flush behavior
 * are retained. NaSeed replaces NuttX inode/sem/IRQ glue with existing
 * BlockDevice/transport/DMA APIs; the full NuttX driver is NOT linked verbatim.
 */
#include "virtio_blk.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <virtio/virtio_blk.h>
#include <stddef.h>
#include <stdint.h>

#define NAS_NUTTX_VQ_SIZE 8U
#define NAS_NUTTX_STATUS_OFFSET 64U
#define NAS_NUTTX_MAX_SPINS 20000000U
#define FEATURE(bit) (UINT64_C(1) << (bit))

static int run_request(struct nas_nuttx_virtio_blk *dev,
                       unsigned type, uint64_t lba, uint32_t bytes,
                       void *buffer) {
    if (!dev || dev->poisoned || !dev->queue.active) return -EIO;
    if (bytes > NAS_DMA_PAGE_BYTES || (bytes && !buffer)) return -EINVAL;
    struct virtio_blk_outhdr *req = dev->header_dma.cpu;
    volatile uint8_t *status =
        (volatile uint8_t *)dev->header_dma.cpu + NAS_NUTTX_STATUS_OFFSET;
    req->type = type;
    req->ioprio = 0;
    req->sector = lba;
    *status = 0xff;
    if (type == VIRTIO_BLK_T_OUT)
        memcpy(dev->payload_dma.cpu, buffer, bytes);
    int rc = nas_dma_sync(dev->transport.dma, &dev->header_dma,
                          0, NAS_NUTTX_STATUS_OFFSET+1U, NAS_DMA_TO_DEVICE);
    if (rc) goto die;
    if (type == VIRTIO_BLK_T_OUT) {
        rc = nas_dma_sync(dev->transport.dma, &dev->payload_dma,
                          0, bytes, NAS_DMA_TO_DEVICE);
        if (rc) goto die;
    }
    const struct nas_vring_segment segs[3] = {
        {dev->header_dma.bus_address, sizeof(*req), false},
        {dev->payload_dma.bus_address, bytes, type == VIRTIO_BLK_T_IN},
        {dev->header_dma.bus_address + NAS_NUTTX_STATUS_OFFSET, 1U, true}
    };
    const struct nas_vring_segment flush[2] = {segs[0], segs[2]};
    uint16_t head;
    rc = nas_vring_queue_submit(&dev->queue,
            type == VIRTIO_BLK_T_FLUSH ? flush : segs,
            type == VIRTIO_BLK_T_FLUSH ? 2U : 3U, &head);
    if (rc) goto die;
    for (unsigned spins = 0; spins < NAS_NUTTX_MAX_SPINS; ++spins) {
        uint16_t completed;
        uint32_t written;
        rc = nas_vring_queue_poll(&dev->queue, &completed, &written);
        if (rc == -EAGAIN) continue;
        if (rc || completed != head || !written) {
            rc = -EIO;
            goto die;
        }
        rc = nas_dma_sync(dev->transport.dma, &dev->header_dma,
                          NAS_NUTTX_STATUS_OFFSET, 1U, NAS_DMA_FROM_DEVICE);
        if (rc) goto die;
        if (*status != VIRTIO_BLK_S_OK) return -EIO;
        if (type == VIRTIO_BLK_T_IN) {
            rc = nas_dma_sync(dev->transport.dma, &dev->payload_dma,
                              0, bytes, NAS_DMA_FROM_DEVICE);
            if (rc) goto die;
            memcpy(buffer, dev->payload_dma.cpu, bytes);
        }
        return 0;
    }
    rc = -ETIMEDOUT;
die:
    /* Never reuse buffers while the device can still DMA: reset on
     * timeout/corruption, retain pinned backing storage until detach. */
    dev->poisoned = true;
    nas_virtio_fail(&dev->transport);
    if (dev->transport.ops && dev->transport.ops->reset)
        dev->transport.ops->reset(dev->transport.context);
    return rc;
}
static int complete_request(struct nas_nuttx_virtio_blk *dev,
                            unsigned type, uint64_t lba, uint32_t bytes,
                            void *buffer) {
    /* One queue plus one bounce page; no concurrent DMA buffer aliasing. */
    if (__atomic_exchange_n(&dev->busy, true, __ATOMIC_ACQUIRE))
        return -EBUSY;
    int rc = run_request(dev, type, lba, bytes, buffer);
    __atomic_store_n(&dev->busy, false, __ATOMIC_RELEASE);
    return rc;
}
static int64_t do_read(void *ctx, uint64_t lba, uint32_t count, void *dst) {
    struct nas_nuttx_virtio_blk *dev = ctx;
    const uint64_t ratio = dev->sector_bytes / 512U;
    int rc = complete_request(dev, VIRTIO_BLK_T_IN,
                              lba * ratio, count * dev->sector_bytes, dst);
    return rc ? rc : (int64_t)count;
}
static int64_t do_write(void *ctx, uint64_t lba, uint32_t count,
                        const void *src) {
    struct nas_nuttx_virtio_blk *dev = ctx;
    const uint64_t ratio = dev->sector_bytes / 512U;
    int rc = complete_request(dev, VIRTIO_BLK_T_OUT,
                              lba * ratio, count * dev->sector_bytes,
                              (void *)src);
    return rc ? rc : (int64_t)count;
}
static int do_flush(void *ctx) {
    struct nas_nuttx_virtio_blk *dev = ctx;
    if (!dev->supports_flush) return -EOPNOTSUPP;
    return complete_request(dev, VIRTIO_BLK_T_FLUSH, 0, 0, NULL);
}
static const struct nas_port_block_ops block_ops={
    .read=do_read,.write=do_write,.flush=do_flush
};

int nas_nuttx_virtio_blk_attach(struct nas_nuttx_virtio_blk *dev,
                                const struct nas_virtio_transport *transport,
                                const char *device_name) {
    if (!dev || !transport || !device_name || !device_name[0] ||
        !transport->dma || !transport->dma->coherent ||
        dev->port.block.registered || dev->queue.active) return -EINVAL;
    dev->transport=*transport; /* Never retain discoverer stack pointer. */
    dev->poisoned=false;
    dev->busy=false;
    uint64_t features;
    int rc=nas_virtio_negotiate(&dev->transport, 0,
            FEATURE(VIRTIO_BLK_F_RO) | FEATURE(VIRTIO_BLK_F_BLK_SIZE) |
            FEATURE(VIRTIO_BLK_F_FLUSH), &features);
    if (rc) return rc;
    uint64_t cap=(uint64_t)nas_virtio_config32(&dev->transport,0) |
                 ((uint64_t)nas_virtio_config32(&dev->transport,4)<<32);
    uint32_t bs = (features & FEATURE(VIRTIO_BLK_F_BLK_SIZE)) ?
            nas_virtio_config32(&dev->transport,
                               offsetof(struct virtio_blk_config,blk_size)) : 512U;
    bool ro = (features & FEATURE(VIRTIO_BLK_F_RO)) != 0;
    dev->supports_flush = (features & FEATURE(VIRTIO_BLK_F_FLUSH)) != 0;
    if (!cap || bs<512U || bs>4096U || (bs&(bs-1U)) ||
        cap < bs/512U || (!ro && !dev->supports_flush)) {
        nas_virtio_fail(&dev->transport);
        return -EOPNOTSUPP;
    }
    dev->capacity_512=cap;
    dev->sector_bytes=bs;
    rc=nas_dma_alloc_page(dev->transport.dma,&dev->header_dma);
    if (rc) goto fail;
    rc=nas_dma_alloc_page(dev->transport.dma,&dev->payload_dma);
    if (rc) goto fail;
    rc=nas_vring_queue_init(&dev->queue,&dev->transport,0,NAS_NUTTX_VQ_SIZE);
    if (rc) goto fail;
    rc=nas_virtio_activate(&dev->transport);
    if (rc) goto fail;
    rc=nas_port_block_attach(&dev->port,device_name,dev,
            cap/(bs/512U),bs,NAS_DMA_PAGE_BYTES/bs,ro,
            NAS_PORT_BLOCK_SECTORS,&block_ops);
    if (!rc) return 0;
fail:
    if (dev->queue.active) (void)nas_vring_queue_close(&dev->queue);
    if (dev->payload_dma.cpu)
        (void)nas_dma_free_page(dev->transport.dma,&dev->payload_dma);
    if (dev->header_dma.cpu)
        (void)nas_dma_free_page(dev->transport.dma,&dev->header_dma);
    if (dev->transport.ops && dev->transport.ops->reset)
        dev->transport.ops->reset(dev->transport.context);
    return rc;
}
int nas_nuttx_virtio_blk_detach(struct nas_nuttx_virtio_blk *dev) {
    if (!dev) return -EINVAL;
    if (__atomic_load_n(&dev->busy, __ATOMIC_ACQUIRE)) return -EBUSY;
    int rc=nas_port_block_detach(&dev->port);
    if (rc) return rc;
    rc=nas_vring_queue_close(&dev->queue);
    if (rc) return rc;
    int a=nas_dma_free_page(dev->transport.dma,&dev->header_dma);
    int b=nas_dma_free_page(dev->transport.dma,&dev->payload_dma);
    return a ? a : b;
}
