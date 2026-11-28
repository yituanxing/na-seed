#ifndef NASEED_LINUX_VIRTGPU_GEM_DMA_PORT_H
#define NASEED_LINUX_VIRTGPU_GEM_DMA_PORT_H
/* Narrow adapter: existing NaSeed DRM GEM page -> provider DMA address.
 * No GPU command implementation or replacement DRM allocator lives here.
 * A pin must survive GPU completion/fence or verified device reset. */
#include "../../kernel/drivers/port/dma.h"
#include "../../kernel/mm/device_backing.h"
#include <stdint.h>
struct nas_linux_virtgpu_gem_page {
    struct nas_vm_device *owner; /* separate GEM object lifetime reference */
    struct nas_dma_mapped_frame dma; /* separate device-accessible page pin */
};
/* Expects a zeroed output; obtains both refs from an existing live GEM handle.
 * A failed DMA rollback may retain an active out->dma and out->owner, which
 * caller MUST release through gem_unmap_page after device quiescence. */
int nas_linux_virtgpu_gem_map_page(unsigned open_file_slot, uint32_t gem_handle,
                                  uint64_t page_index,
                                  const struct nas_dma_provider *,
                                  struct nas_linux_virtgpu_gem_page *out);
/* Caller must have waited for the GPU used-ring completion/fence, or reset
 * the device and proved no DMA is possible, before unmapping this page. */
int nas_linux_virtgpu_gem_unmap_page(struct nas_linux_virtgpu_gem_page *);
#endif
