#ifndef NASEED_DRIVER_DMA_H
#define NASEED_DRIVER_DMA_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../../errno.h"
/* PAGE-size device-accessible allocation. bus_address is NOT necessarily
 * a CPU physical address: IOMMU-capable providers are allowed later.
 * The provider owns mapping/coherency, drivers own queue lifetimes.
 */
#define NAS_DMA_PAGE_BYTES 4096U
struct nas_dma_page {
    uint64_t bus_address;
    uint64_t frame_pa; /* provider-private release cookie for QEMU backend */
    void *cpu;
    uint32_t bytes;
};
enum nas_dma_direction {
    NAS_DMA_TO_DEVICE = 1,
    NAS_DMA_FROM_DEVICE = 2
};
struct nas_dma_provider {
    void *context;
    bool coherent;
    int (*alloc_page)(void *, struct nas_dma_page *);
    int (*release_page)(void *, struct nas_dma_page *);
    int (*sync)(void *, struct nas_dma_page *, uint32_t offset,
                uint32_t bytes, enum nas_dma_direction);
    /* Optional ownership-safe map of a BORROWED physical frame.
     * map_frame must acquire a page pin before returning the device bus
     * address; unmap_frame releases it only after device quiescence.
     * Never assume bus_address == frame_pa on a generic provider. */
    int (*map_frame)(void *, uint64_t frame_pa, uint64_t *bus_address);
    int (*unmap_frame)(void *, uint64_t frame_pa, uint64_t bus_address);
};
/* One borrowed page pin and DMA mapping. The map/unmap provider owns the
 * pin, NOT the caller's original frame reference. This representation is
 * deliberately separate from provider-allocated nas_dma_page ownership. */
struct nas_dma_mapped_frame {
    const struct nas_dma_provider *provider;
    uint64_t frame_pa, bus_address;
    bool active;
    /* Failed/overflowed mappings can retain a pin but are not usable. */
    bool usable;
};
/* An explicit, bounded software ownership lease for one DMA-capable device.
 * A controller must arm the lease BEFORE a write that might enable bus
 * mastering and disarm it ONLY AFTER verified device quiescence. While armed,
 * even an accidental cleanup path cannot release any of its DMA pages.
 * This is NOT an IOMMU, physical DMA isolation or a coherent-memory proof. */
#define NAS_DMA_LEASE_PAGE_LIMIT 8U
struct nas_dma_lease {
    const struct nas_dma_provider *provider;
    struct nas_dma_page *pages[NAS_DMA_LEASE_PAGE_LIMIT];
    unsigned held;
    bool may_dma;
};
int nas_dma_lease_begin(struct nas_dma_lease *,
                        const struct nas_dma_provider *);
int nas_dma_lease_alloc_page(struct nas_dma_lease *,struct nas_dma_page *);
int nas_dma_lease_sync(struct nas_dma_lease *,struct nas_dma_page *,
                       uint32_t,uint32_t,enum nas_dma_direction);
int nas_dma_lease_arm(struct nas_dma_lease *);
int nas_dma_lease_disarm_after_quiesce(struct nas_dma_lease *);
int nas_dma_lease_free_page(struct nas_dma_lease *,struct nas_dma_page *);
int nas_dma_lease_end(struct nas_dma_lease *);
int nas_dma_alloc_page(const struct nas_dma_provider *, struct nas_dma_page *);
/* If a provider allocates an invalid descriptor and its rollback release
 * fails, allocation returns that release error and leaves page.cpu non-NULL
 * when one was supplied. The caller must retry nas_dma_free_page() before
 * reusing this descriptor; failed allocation does not always mean no owner.
 * Providers must never claim success with a missing CPU mapping. */
int nas_dma_free_page(const struct nas_dma_provider *, struct nas_dma_page *);
int nas_dma_sync(const struct nas_dma_provider *, struct nas_dma_page *,
                 uint32_t, uint32_t, enum nas_dma_direction);
/* Borrowed-frame DMA mappings for GPU GEM/shmem pages. Require an explicit
 * provider, coherent memory and valid managed frame pins; map must remain
 * alive through all GPU fences / used-ring completions, or device reset.
 * Noncoherent and IOMMU providers need their own correctly paired callbacks. */
int nas_dma_map_borrowed_frame(const struct nas_dma_provider *, uint64_t frame_pa,
                               struct nas_dma_mapped_frame *);
int nas_dma_unmap_borrowed_frame(struct nas_dma_mapped_frame *);
/* Slice a live provider-owned pin into an SG descriptor; all offsets are
 * checked before a queue may publish the device-visible address. */
static inline int nas_dma_mapped_slice(const struct nas_dma_mapped_frame *map,
                                      uint32_t offset,uint32_t length,
                                      uint64_t *bus_address) {
    if (!map || !map->active || !map->usable || !map->provider || !bus_address ||
        !length || offset>=NAS_DMA_PAGE_BYTES ||
        length>NAS_DMA_PAGE_BYTES-offset)return -EINVAL;
    *bus_address=map->bus_address+offset;
    return 0;
}
/* QEMU virt direct DMA identity mapping + coherent emulated guest RAM.
 * Do not use as an implementation for real non-coherent physical hardware. */
extern const struct nas_dma_provider nas_dma_qemu_coherent;
#endif
