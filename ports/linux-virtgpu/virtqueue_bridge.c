#include "virtqueue_bridge.h"
#include "../../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>

int nas_linux_virtgpu_vq_bind(struct nas_linux_virtgpu_vq_bridge *b,
                             struct nas_vring_queue *q) {
    if (!b || !q || !q->active || q->poisoned ||
        q->inflight || q->kick_pending || !q->ring.num ||
        q->ring.num > NAS_VRING_MAX_DESC) return -EINVAL;
    *b=(struct nas_linux_virtgpu_vq_bridge){0};
    b->queue=q;
    return 0;
}
int nas_linux_virtgpu_vq_submit(struct nas_linux_virtgpu_vq_bridge *b,
                               const struct nas_vring_segment *segs,
                               unsigned n, void *cookie) {
    if (!b || !b->queue || !cookie || !segs || !n)return -EINVAL;
    struct nas_vring_queue *q=b->queue;
    if (!q->active || q->poisoned)return -EIO;
    /* One cookie per descriptor-chain head. The caller serializes this call,
     * its completion drain, and the upstream kick_prepare operation. */
    if (q->free_count &&
        (q->free_head>=NAS_VRING_MAX_DESC || b->cookies[q->free_head]))
        return -EIO;
    uint16_t head=UINT16_MAX;
    int rc=nas_vring_queue_submit_deferred(q,segs,n,&head);
    if (rc) {
        /* Post-publication sync failures leave descriptors owned by device.
         * Retain cookie through transport reset; no premature free on -EIO. */
        if (q->poisoned && head<NAS_VRING_MAX_DESC &&
            q->pending[head] && !b->cookies[head])
            b->cookies[head]=cookie;
        return rc;
    }
    if (head>=NAS_VRING_MAX_DESC || b->cookies[head]) {
        /* A published chain may still DMA: poison, never free its owner. */
        q->poisoned=true;
        nas_virtio_fail(q->transport);
        return -EIO;
    }
    b->cookies[head]=cookie;
    return 0;
}
int nas_linux_virtgpu_vq_reclaim(struct nas_linux_virtgpu_vq_bridge *b,
                                void **cookie, uint32_t *used_bytes) {
    if (!cookie || !used_bytes)return -EINVAL;
    *cookie=NULL;
    *used_bytes=0;
    if (!b || !b->queue)return -EINVAL;
    uint16_t head=0;
    int rc=nas_vring_queue_poll(b->queue,&head,used_bytes);
    if (rc)return rc;
    if (head>=NAS_VRING_MAX_DESC || !b->cookies[head]) {
        b->queue->poisoned=true;
        nas_virtio_fail(b->queue->transport);
        return -EIO;
    }
    *cookie=b->cookies[head];
    b->cookies[head]=NULL;
    return 0;
}
int nas_linux_virtgpu_vq_reap_after_reset(struct nas_linux_virtgpu_vq_bridge *b,
                                         unsigned *cursor, void **cookie) {
    if (!b || !b->queue || !cursor || !cookie ||
        *cursor>NAS_VRING_MAX_DESC)return -EINVAL;
    *cookie=NULL;
    /* Closing the queue resets the device before retiring its DMA ring.
     * Never hand a request back while any device DMA may still be live. */
    if (b->queue->active || b->queue->dma_page.cpu)return -EBUSY;
    for (unsigned i=*cursor;i<NAS_VRING_MAX_DESC;i++) {
        if (!b->cookies[i])continue;
        *cookie=b->cookies[i];
        b->cookies[i]=NULL;
        *cursor=i+1U;
        return 0;
    }
    return -ENOENT;
}
bool nas_linux_virtgpu_vq_kick_prepare(struct nas_linux_virtgpu_vq_bridge *b) {
    return b && b->queue && nas_vring_queue_kick_prepare(b->queue);
}
void nas_linux_virtgpu_vq_notify(struct nas_linux_virtgpu_vq_bridge *b) {
    if (b && b->queue) nas_vring_queue_notify(b->queue);
}
void nas_linux_virtgpu_vq_disable_callbacks(struct nas_linux_virtgpu_vq_bridge *b) {
    if (b && b->queue) nas_vring_queue_disable_callbacks(b->queue);
}
bool nas_linux_virtgpu_vq_enable_callbacks(struct nas_linux_virtgpu_vq_bridge *b) {
    return b && b->queue && nas_vring_queue_enable_callbacks(b->queue);
}
