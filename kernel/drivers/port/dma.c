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
    *page = (struct nas_dma_page){0};
    int rc = provider->alloc_page(provider->context, page);
    if (rc) {
        /* A failing provider must rollback partial resources itself. */
        *page = (struct nas_dma_page){0};
        return rc;
    }
    if (!page->cpu || !page->bus_address || page->bytes != NAS_DMA_PAGE_BYTES) {
        (void)provider->release_page(provider->context, page);
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
