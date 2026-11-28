#include "gem_dma_port.h"
#include "../../kernel/fs/drm_gem.h"
#include "../../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>

int nas_linux_virtgpu_gem_map_page(unsigned slot,uint32_t handle,
                                  uint64_t index,
                                  const struct nas_dma_provider *provider,
                                  struct nas_linux_virtgpu_gem_page *out) {
    if (!out || !provider || !handle || index>UINT64_MAX/NAS_DMA_PAGE_BYTES)
        return -EINVAL;
    if (out->owner || out->dma.active || out->dma.provider)return -EBUSY;
    uint64_t base=0;
    int rc=nas_drm_gem_map_offset(slot,handle,&base);
    if (rc)return rc;
    const uint64_t delta=index*NAS_DMA_PAGE_BYTES;
    if (base>UINT64_MAX-delta)return -EOVERFLOW;
    struct nas_vm_device *owner=NULL;
    rc=nas_drm_gem_mmap_pin(slot,base,&owner);
    if (rc)return rc;
    if (!owner || !owner->page || !owner->put || !owner->context) {
        /* A successful GEM mmap pin guarantees a valid owner; fail closed.
         * If provider breaks that contract, do not call missing callbacks. */
        if (owner && owner->put)owner->put(owner->context);
        return -EIO;
    }
    uint64_t frame=0;
    rc=owner->page(owner->context,base+delta,&frame);
    if (!rc)rc=nas_dma_map_borrowed_frame(provider,frame,&out->dma);
    if (rc && !out->dma.active) {
        owner->put(owner->context);
        return rc;
    }
    /* Even if the DMA mapping failed with a retained pin, hold the GEM
     * object until the caller successfully releases that pin. */
    out->owner=owner;
    return rc;
}
int nas_linux_virtgpu_gem_unmap_page(struct nas_linux_virtgpu_gem_page *pin) {
    if (!pin || !pin->owner || !pin->dma.active)return -EINVAL;
    int rc=nas_dma_unmap_borrowed_frame(&pin->dma);
    if (rc)return rc; /* Never release GEM while the mapping is live. */
    pin->owner->put(pin->owner->context);
    pin->owner=NULL;
    return 0;
}
