/* SPDX-License-Identifier: GPL-2.0 */
/* Linux v6.14 internal API layout/signatures; NaSeed port subset. */
#ifndef NASEED_LINUX_VIRTGPU_SCATTERLIST_PORT_H
#define NASEED_LINUX_VIRTGPU_SCATTERLIST_PORT_H
/* Freestanding subset of Linux v6.14 scatterlist ABI.
 * Preserve the exact upstream LP64 struct scatterlist and sg_table layout.
 * Ownership of NaSeed DMA page pins belongs to the per-virtqueue adapter,
 * NEVER to additional fields on Linux's original scatterlist struct.
 *
 * sg_init_one() of arbitrary heap pointers remains unavailable: allocating
 * a scatterlist does not map its memory into a device DMA address space.
 * The port registers proven provider DMA mappings separately and rejects
 * any unregistered source, rather than guessing virt_to_phys().
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define SG_CHAIN 1UL
#define SG_END 2UL
#define SG_PAGE_LINK_MASK (SG_CHAIN | SG_END)
struct scatterlist {
    unsigned long page_link;
    unsigned int offset;
    unsigned int length;
    uint64_t dma_address;
};
struct sg_table {
    struct scatterlist *sgl;
    unsigned int nents;
    unsigned int orig_nents;
};
_Static_assert(sizeof(struct scatterlist)==24U,"Linux v6.14 LP64 SG layout");
_Static_assert(offsetof(struct scatterlist,dma_address)==16U,"Linux DMA SG offset");
#define sg_dma_address(sg) ((sg)->dma_address)
#define sg_dma_len(sg) ((sg)->length)
static inline void sg_init_table(struct scatterlist *sg,unsigned n) {
    if (!sg || !n)return;
    for(unsigned i=0;i<n;i++)sg[i]=(struct scatterlist){0};
    sg[n-1U].page_link|=SG_END;
}
static inline struct scatterlist *sg_next(struct scatterlist *sg) {
    if (!sg || (sg->page_link & SG_END))return NULL;
    sg++;
    if (sg->page_link & SG_CHAIN)
        return (struct scatterlist *)(sg->page_link & ~SG_PAGE_LINK_MASK);
    return sg;
}
#endif
