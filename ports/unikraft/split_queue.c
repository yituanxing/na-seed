#include "split_queue.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stdint.h>
#include <stddef.h>

static bool power2(unsigned n) { return n && !(n & (n - 1U)); }
static void broken(struct nas_vring_queue *q) {
    q->poisoned = true;
    nas_virtio_fail(q->transport);
}

int nas_vring_queue_init(struct nas_vring_queue *q,
                         const struct nas_virtio_transport *transport,
                         unsigned queue_id, unsigned num_desc) {
    if (!q || !transport || !transport->dma ||
        !num_desc || num_desc > NAS_VRING_MAX_DESC || !power2(num_desc) ||
        queue_id > UINT16_MAX || !transport->dma->coherent ||
        vring_size(num_desc, NAS_VRING_ALIGN) > NAS_DMA_PAGE_BYTES)
        return -EINVAL;
    memset(q, 0, sizeof(*q));
    int rc = nas_dma_alloc_page(transport->dma, &q->dma_page);
    if (rc) return rc;
    /* Upstream vring_init aligns CPU pointers. The bus-visible used ring
     * must have the same offsets even behind an IOMMU mapping. */
    if (((uintptr_t)q->dma_page.cpu & (NAS_VRING_ALIGN-1U)) ||
        (q->dma_page.bus_address & (NAS_VRING_ALIGN-1U))) {
        rc = -EINVAL;
        goto fail;
    }
    memset(q->dma_page.cpu, 0, q->dma_page.bytes);
    vring_init(&q->ring, num_desc, q->dma_page.cpu, NAS_VRING_ALIGN);
    for (unsigned i = 0; i < num_desc; ++i)
        q->ring.desc[i].next = (i + 1U < num_desc) ? (uint16_t)(i + 1U) : UINT16_MAX;
    q->transport = transport;
    q->queue_id = (uint16_t)queue_id;
    q->free_count = (uint16_t)num_desc;
    rc = nas_dma_sync(transport->dma, &q->dma_page, 0,
                      q->dma_page.bytes, NAS_DMA_TO_DEVICE);
    if (rc) goto fail;
    const uint64_t base = q->dma_page.bus_address;
    const uintptr_t cpu = (uintptr_t)q->dma_page.cpu;
    rc = nas_virtio_setup_queue(transport, queue_id, num_desc,
                base + (uintptr_t)q->ring.desc - cpu,
                base + (uintptr_t)q->ring.avail - cpu,
                base + (uintptr_t)q->ring.used - cpu);
    if (rc) {
        /* A partially configured device must stop DMA before freeing rings. */
        if (transport->ops && transport->ops->reset)
            transport->ops->reset(transport->context);
        goto fail;
    }
    q->active = true;
    return 0;
fail:
    (void)nas_dma_free_page(transport->dma, &q->dma_page);
    memset(q, 0, sizeof(*q));
    return rc;
}

int nas_vring_queue_submit(struct nas_vring_queue *q,
                           const struct nas_vring_segment *segs, unsigned n,
                           uint16_t *out_head) {
    if (!q || !q->active || !segs || !out_head || !n)
        return -EINVAL;
    if (q->poisoned) return -EIO;
    if (n > q->free_count) return -ENOSPC;
    for (unsigned i=0;i<n;++i)
        if (!segs[i].length) return -EINVAL;
    struct vring *r = &q->ring;
    uint16_t head=q->free_head, current=head;
    for (unsigned i=0;i<n;++i) {
        struct vring_desc *d=&r->desc[current];
        uint16_t next=d->next;
        d->addr=segs[i].bus_address;
        d->len=segs[i].length;
        d->flags=(segs[i].device_writes ? VRING_DESC_F_WRITE : 0) |
                 (i+1U<n ? VRING_DESC_F_NEXT : 0);
        d->next=(i+1U<n) ? next : 0;
        current=next;
    }
    q->free_head=current;
    q->free_count=(uint16_t)(q->free_count-n);
    q->pending[head]=1;
    q->inflight++;
    uint16_t av_idx=r->avail->idx;
    r->avail->ring[av_idx % r->num]=head;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r->avail->idx=(uint16_t)(av_idx+1U);
    int rc=nas_dma_sync(q->transport->dma, &q->dma_page, 0,
                        q->dma_page.bytes, NAS_DMA_TO_DEVICE);
    /* A failed DMA sync cannot be retried by the caller: the submitted
     * chain already belongs to the device. Mark it failed and reset on
     * close, never free live descriptors here. */
    if (rc) { broken(q); return rc; }
    if (!(r->used->flags & VRING_USED_F_NO_NOTIFY))
        nas_virtio_notify(q->transport, q->queue_id);
    *out_head=head;
    return 0;
}

int nas_vring_queue_poll(struct nas_vring_queue *q, uint16_t *head,
                         uint32_t *bytes) {
    if (!q || !q->active || !head || !bytes) return -EINVAL;
    if (q->poisoned) return -EIO;
    int rc=nas_dma_sync(q->transport->dma, &q->dma_page, 0,
                        q->dma_page.bytes, NAS_DMA_FROM_DEVICE);
    if (rc) {broken(q);return rc;}
    struct vring *r=&q->ring;
    uint16_t idx=r->used->idx;
    if (idx==q->last_used) return -EAGAIN;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    struct vring_used_elem *elem=&r->used->ring[q->last_used % r->num];
    uint32_t id=elem->id;
    if (id>=r->num || !q->pending[id] ||
        (uint16_t)(idx-q->last_used)>q->inflight) {
        broken(q);
        return -EIO;
    }
    /* Validate the whole chain before updating the free-list: malicious
     * device completion must never poison allocator state. */
    uint16_t current=(uint16_t)id, tail=current;
    unsigned count=0;
    while (1) {
        if (++count>r->num) {broken(q);return -EIO;}
        struct vring_desc *d=&r->desc[current];
        if (!(d->flags & VRING_DESC_F_NEXT)) {tail=current;break;}
        if (d->next>=r->num) {broken(q);return -EIO;}
        current=d->next;
    }
    if (count > r->num - q->free_count) {broken(q);return -EIO;}
    r->desc[tail].next=q->free_head;
    q->free_head=(uint16_t)id;
    q->free_count=(uint16_t)(q->free_count+count);
    q->pending[id]=0;
    q->inflight--;
    q->last_used++;
    *head=(uint16_t)id;
    *bytes=elem->len;
    return 0;
}

int nas_vring_queue_close(struct nas_vring_queue *q) {
    if (!q || !q->active) return -EINVAL;
    /* Reset first even with zero inflight: the device still owns ring
     * addresses until reset, and late completion is otherwise possible. */
    if (!q->transport || !q->transport->ops || !q->transport->ops->reset)
        return -EOPNOTSUPP;
    q->transport->ops->reset(q->transport->context);
    int rc=nas_dma_free_page(q->transport->dma, &q->dma_page);
    if (!rc) memset(q, 0, sizeof(*q));
    return rc;
}
