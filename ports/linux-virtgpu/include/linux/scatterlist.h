#ifndef NASEED_LINUX_VIRTGPU_SCATTERLIST_PORT_H
#define NASEED_LINUX_VIRTGPU_SCATTERLIST_PORT_H
/* Freestanding, intentionally bounded Linux scatterlist API subset.
 * First four fields match Linux v6.14's struct scatterlist on LP64,
 * with one NaSeed-private mapping owner field AFTER the upstream layout.
 * The original Linux GPU driver source is never changed by this adapter.
 *
 * A Linux sg_init_one() from arbitrary kernel virtual memory does NOT yet
 * produce a DMA mapping. Every submitted SG must be explicitly mapped to a
 * pinned physical frame by nas_linux_virtgpu_sg_map(); fail closed otherwise.
 * A completed SG descriptor does not release persistent GPU backing pages.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct nas_dma_mapped_frame;

#define SG_CHAIN 1UL
#define SG_END 2UL
#define SG_PAGE_LINK_MASK (SG_CHAIN | SG_END)
struct scatterlist {
    unsigned long page_link;
    unsigned int offset;
    unsigned int length;
    uint64_t dma_address;
    /* Port-specific ownership proof; not part of original Linux struct. */
    const struct nas_dma_mapped_frame *nas_dma_owner;
};
struct sg_table {
    struct scatterlist *sgl;
    unsigned int nents;
    unsigned int orig_nents;
};
#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg) ((sg)->length)

static inline void sg_init_table(struct scatterlist *sg,unsigned n) {
    if (!sg || !n)return;
    for(unsigned i=0;i<n;i++)
        sg[i]=(struct scatterlist){0};
    sg[n-1U].page_link|=SG_END;
}
static inline struct scatterlist *sg_next(struct scatterlist *sg) {
    if (!sg || (sg->page_link & SG_END))return NULL;
    sg++;
    if (sg->page_link & SG_CHAIN)
        return (struct scatterlist *)(sg->page_link & ~SG_PAGE_LINK_MASK);
    return sg;
}
/* The map is valid only while the associated DMA pin remains active and
 * usable. The caller must NOT unmap it until the GPU has stopped using it. */
int nas_linux_virtgpu_sg_map(struct scatterlist *,
                            const struct nas_dma_mapped_frame *,
                            uint32_t offset, uint32_t length);
#endif
