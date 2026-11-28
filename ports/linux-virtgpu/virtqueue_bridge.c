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
    uint16_t head=0;
    int rc=nas_vring_queue_submit_deferred(q,segs,n,&head);
    if (rc)return rc;
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
bool nas_linux_virtgpu_vq_kick_prepare(struct nas_linux_virtgpu_vq_bridge *b) {
    return b && b->queue && nas_vring_queue_kick_prepare(b->queue);
}
void nas_linux_virtgpu_vq_notify(struct nas_linux_virtgpu_vq_bridge *b) {
    if (b && b->queue) nas_vring_queue_notify(b->queue);
}
