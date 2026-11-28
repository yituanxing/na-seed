#include "vq_pair_port.h"
#include "../../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>

/* The original Linux virtgpu_kms.c passes exactly two vq infos: control and
 * cursor. Both are backed by the existing Unikraft split vring and the real
 * NaSeed transport, never a second independent GPU transport. */
int nas_linux_virtgpu_vq_pair_init(
    struct nas_linux_virtgpu_vq_pair *pair,
    const struct nas_virtio_transport *t,struct virtio_device *vdev,
    unsigned depth,void (*ctrl)(struct virtqueue *),
    void (*cursor)(struct virtqueue *)) {
    if(!pair || !t || !depth || depth>NAS_VRING_MAX_DESC ||
       (depth & (depth-1U)) || pair->initialized ||
       pair->rings[0].active || pair->rings[1].active ||
       pair->rings[0].dma_page.cpu || pair->rings[1].dma_page.cpu ||
       pair->transport)return -EINVAL;
    pair->transport=t;
    int rc=nas_vring_queue_init(&pair->rings[0],t,0U,depth);
    if(rc)goto fail;
    rc=nas_vring_queue_init(&pair->rings[1],t,1U,depth);
    if(rc)goto fail;
    rc=nas_linux_virtgpu_virtqueue_bind(&pair->vqs[0],
                                       &pair->rings[0],vdev,0U);
    if(rc)goto fail;
    rc=nas_linux_virtgpu_virtqueue_bind(&pair->vqs[1],
                                       &pair->rings[1],vdev,1U);
    if(rc)goto fail;
    pair->vqs[0].linux_vq.name="control";
    pair->vqs[1].linux_vq.name="cursor";
    pair->vqs[0].linux_vq.callback=ctrl;
    pair->vqs[1].linux_vq.callback=cursor;
    pair->initialized=true;
    return 0;
fail:
    /* A partially configured hardware queue may still DMA. The real
     * close() resets the controller before releasing ring memory. */
    if(pair->rings[1].active || pair->rings[1].dma_page.cpu)
        (void)nas_vring_queue_close(&pair->rings[1]);
    if(pair->rings[0].active || pair->rings[0].dma_page.cpu)
        (void)nas_vring_queue_close(&pair->rings[0]);
    /* Keep the transport pointer if a DMA page could not be released.
     * Otherwise allow a clean retry of the complete two-queue setup. */
    if(!pair->rings[0].dma_page.cpu && !pair->rings[1].dma_page.cpu)
        pair->transport=NULL;
    return rc;
}
int nas_linux_virtgpu_vq_pair_activate(struct nas_linux_virtgpu_vq_pair *pair) {
    if(!pair || !pair->initialized || !pair->transport ||
       !pair->rings[0].active || !pair->rings[1].active)return -EINVAL;
    return nas_virtio_activate(pair->transport);
}
uint32_t nas_linux_virtgpu_vq_pair_irq_ack(
    struct nas_linux_virtgpu_vq_pair *pair) {
    if(!pair || !pair->initialized || !pair->transport)return 0U;
    /* PCI reads and clears its ISR; MMIO reads the status and writes ACK.
     * Both follow the existing shared NaSeed transport, not invented GPU
     * interrupt registers. Never invoke dequeue work in the hard IRQ. */
    uint32_t asserted=nas_virtio_irq_ack(pair->transport);
    nas_linux_virtgpu_vq_pair_irq_record(pair,asserted);
    return asserted;
}
void nas_linux_virtgpu_vq_pair_irq_record(
    struct nas_linux_virtgpu_vq_pair *pair,uint32_t asserted) {
    if(!pair || !pair->initialized)return;
    __atomic_fetch_or(&pair->irq_pending,
        asserted&(NAS_VIRTIO_IRQ_USED_BUFFER|NAS_VIRTIO_IRQ_CONFIG_CHANGE),
        __ATOMIC_RELEASE);
}
int nas_linux_virtgpu_vq_pair_service(struct nas_linux_virtgpu_vq_pair *pair) {
    if(!pair || !pair->initialized || !pair->transport)return -EINVAL;
    uint32_t pending=__atomic_exchange_n(&pair->irq_pending,0U,
                                          __ATOMIC_ACQUIRE);
    if(!(pending & NAS_VIRTIO_IRQ_USED_BUFFER))return 0;
    int callbacks=0;
    for(unsigned i=0;i<2U;i++) {
        struct nas_vring_queue *ring=&pair->rings[i];
        if(!ring->active || ring->poisoned)return -EIO;
        int rc=nas_dma_sync(ring->transport->dma,&ring->dma_page,0U,
                            ring->dma_page.bytes,NAS_DMA_FROM_DEVICE);
        if(rc)return rc;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        /* The Linux callback schedules process-context dequeue_work.
         * Never call it for an empty used ring, or in hard IRQ. */
        if(ring->ring.used->idx!=ring->last_used &&
           pair->vqs[i].linux_vq.callback) {
            pair->vqs[i].linux_vq.callback(&pair->vqs[i].linux_vq);
            callbacks++;
        }
    }
    return callbacks;
}
int nas_linux_virtgpu_vq_pair_close(struct nas_linux_virtgpu_vq_pair *pair) {
    if(!pair || !pair->transport)return -EINVAL;
    /* No IRQ or driver callbacks may run concurrently with close.
     * Reset MUST precede any transport DMA page release. */
    if(pair->transport->ops && pair->transport->ops->reset)
        pair->transport->ops->reset(pair->transport->context);
    int rc0=0,rc1=0;
    if(pair->rings[0].active || pair->rings[0].dma_page.cpu)
        rc0=nas_vring_queue_close(&pair->rings[0]);
    if(pair->rings[1].active || pair->rings[1].dma_page.cpu)
        rc1=nas_vring_queue_close(&pair->rings[1]);
    if(rc0 || rc1)return rc0?rc0:rc1;
    pair->initialized=false;
    pair->irq_pending=0U;
    /* Retain vqs[i].bridge cookies for reset-only abandoned-cookie recovery.
     * The outer device owner can clear/reinitialize after reaping them. */
    return 0;
}
