#include "dma.h"
#include "../../mm/frame.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>

static int qemu_alloc(void *ctx, struct nas_dma_page *page) {
    (void)ctx;
    uint64_t pa = nas_frame_alloc();
    if (!pa) return -ENOMEM;
    page->frame_pa = pa;
    page->bus_address = pa; /* only valid under QEMU identity DMA */
    page->cpu = (void *)(uintptr_t)(nas_hhdm_offset() + pa);
    page->bytes = NAS_DMA_PAGE_BYTES;
    return 0;
}
static int qemu_free(void *ctx, struct nas_dma_page *page) {
    (void)ctx;
    return nas_frame_put(page->frame_pa) ? 0 : -EIO;
}
const struct nas_dma_provider nas_dma_qemu_coherent = {
    .coherent = true, .alloc_page = qemu_alloc, .release_page = qemu_free
};
int nas_dma_alloc_page(const struct nas_dma_provider *provider,
                       struct nas_dma_page *page) {
    if (!provider || !page || !provider->alloc_page ||
        !provider->release_page || (!provider->coherent && !provider->sync))
        return -EINVAL;
    /* Retrying allocation must not overwrite a page still owned by the
     * caller after an earlier provider-release failure. */
    if (page->cpu) return -EBUSY;
    *page = (struct nas_dma_page){0};
    int rc = provider->alloc_page(provider->context, page);
    if (rc) {
        /* A failing provider must rollback partial resources itself. */
        *page = (struct nas_dma_page){0};
        return rc;
    }
    /* DMA bus address 0 is valid with some IOMMU mappings; the CPU
     * alias and mapped size, not a zero sentinel, establish validity. */
    if (!page->cpu || page->bytes != NAS_DMA_PAGE_BYTES) {
        int release_rc = provider->release_page(provider->context, page);
        /* A provider can fail its release. Preserve the descriptor, so
         * callers can retry nas_dma_free_page() before reusing storage.
         * A provider that reports success without a CPU alias violates
         * the allocation contract and cannot be retried through callers'
         * normal cpu-pointer ownership checks. */
        if (release_rc) return release_rc;
        *page = (struct nas_dma_page){0};
        return -EIO;
    }
    return 0;
}
int nas_dma_free_page(const struct nas_dma_provider *provider,
                      struct nas_dma_page *page) {
    if (!provider || !provider->release_page || !page || !page->cpu)
        return -EINVAL;
    int rc = provider->release_page(provider->context, page);
    if (!rc) *page = (struct nas_dma_page){0};
    return rc;
}
int nas_dma_sync(const struct nas_dma_provider *provider,
                 struct nas_dma_page *page, uint32_t offset, uint32_t bytes,
                 enum nas_dma_direction direction) {
    if (!provider || !page || !page->cpu ||
        page->bytes != NAS_DMA_PAGE_BYTES ||
        (direction != NAS_DMA_TO_DEVICE && direction != NAS_DMA_FROM_DEVICE) ||
        offset > page->bytes || bytes > page->bytes - offset)
        return -EINVAL;
    if (provider->coherent) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        return 0;
    }
    if (!provider->sync) return -EOPNOTSUPP;
    return provider->sync(provider->context, page, offset, bytes, direction);
}

/* Keep queue pages attributed to exactly one device session, independently
 * of the transport-specific allocator. The guard is deliberately simple:
 * the caller must prove quiescence, it cannot infer that from a timeout. */
static int lease_find(const struct nas_dma_lease *lease,
                      const struct nas_dma_page *page) {
    if (!lease || !page || lease->held > NAS_DMA_LEASE_PAGE_LIMIT)
        return -1;
    for (unsigned i=0;i<lease->held;i++)
        if (lease->pages[i]==page) return (int)i;
    return -1;
}
int nas_dma_lease_begin(struct nas_dma_lease *lease,
                        const struct nas_dma_provider *provider) {
    if (!lease || !provider || !provider->alloc_page ||
        !provider->release_page || (!provider->coherent && !provider->sync))
        return -EINVAL;
    if (lease->provider || lease->held || lease->may_dma) return -EBUSY;
    *lease=(struct nas_dma_lease){.provider=provider};
    return 0;
}
int nas_dma_lease_alloc_page(struct nas_dma_lease *lease,
                             struct nas_dma_page *page) {
    if (!lease || !lease->provider || !page) return -EINVAL;
    if (lease->may_dma || page->cpu || lease_find(lease,page)>=0)
        return -EBUSY; /* never adopt another lease's live page */
    if (lease->held==NAS_DMA_LEASE_PAGE_LIMIT) return -ENOSPC;
    int rc=nas_dma_alloc_page(lease->provider,page);
    /* A provider can fail to roll back an invalid descriptor. If it retains
     * a CPU alias, preserve the lease owner so cleanup can be retried. */
    if (!rc || page->cpu) lease->pages[lease->held++]=page;
    return rc;
}
int nas_dma_lease_sync(struct nas_dma_lease *lease,
                       struct nas_dma_page *page,uint32_t offset,
                       uint32_t bytes,enum nas_dma_direction direction) {
    if (!lease || !lease->provider || lease_find(lease,page)<0)
        return -EINVAL;
    return nas_dma_sync(lease->provider,page,offset,bytes,direction);
}
int nas_dma_lease_arm(struct nas_dma_lease *lease) {
    if (!lease || !lease->provider || !lease->held) return -EINVAL;
    if (lease->may_dma) return -EBUSY;
    lease->may_dma=true;
    return 0;
}
int nas_dma_lease_disarm_after_quiesce(struct nas_dma_lease *lease) {
    if (!lease || !lease->provider || !lease->may_dma) return -EINVAL;
    /* ONLY a caller that verified the controller cannot DMA may do this. */
    lease->may_dma=false;
    return 0;
}
int nas_dma_lease_free_page(struct nas_dma_lease *lease,
                            struct nas_dma_page *page) {
    if (!lease || !lease->provider) return -EINVAL;
    if (lease->may_dma) return -EBUSY;
    int i=lease_find(lease,page);
    if (i<0) return -EINVAL;
    int rc=nas_dma_free_page(lease->provider,page);
    if (rc) return rc; /* preserve ownership for a retry */
    for (unsigned j=(unsigned)i+1U;j<lease->held;j++)
        lease->pages[j-1U]=lease->pages[j];
    lease->pages[--lease->held]=NULL;
    return 0;
}
int nas_dma_lease_end(struct nas_dma_lease *lease) {
    if (!lease || !lease->provider) return -EINVAL;
    if (lease->may_dma || lease->held) return -EBUSY;
    *lease=(struct nas_dma_lease){0};
    return 0;
}
