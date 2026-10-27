#ifndef NASEED_DRIVER_DMA_H
#define NASEED_DRIVER_DMA_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
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
};
int nas_dma_alloc_page(const struct nas_dma_provider *, struct nas_dma_page *);
int nas_dma_free_page(const struct nas_dma_provider *, struct nas_dma_page *);
int nas_dma_sync(const struct nas_dma_provider *, struct nas_dma_page *,
                 uint32_t, uint32_t, enum nas_dma_direction);
/* QEMU virt direct DMA identity mapping + coherent emulated guest RAM.
 * Do not use as an implementation for real non-coherent physical hardware. */
extern const struct nas_dma_provider nas_dma_qemu_coherent;
#endif
