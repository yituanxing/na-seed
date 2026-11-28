#ifndef NASEED_DRM_GEM_H
#define NASEED_DRM_GEM_H
#include <stdint.h>
struct nas_vm_device;
struct nas_drm_gem_created {
    uint32_t handle,pitch;
    uint64_t size;
};
/* Shared OpenFile slot, not a process-local FD, owns GEM handles.
 * Separate VMA references keep mapped physical pages alive after destroy,
 * close, dup/fork and final shared OpenFile teardown. */
int nas_drm_gem_create(unsigned slot,uint32_t width,uint32_t height,
                       uint32_t bpp,uint32_t flags,
                       struct nas_drm_gem_created *out);
/* Experimental TGOSKits BO uses existing GEM/VMA memory ownership. */
int nas_drm_gem_tgos_backing_create(unsigned slot,uint32_t handle,uint32_t bytes);
int nas_drm_gem_tgos_backing_count(unsigned slot,uint32_t handle,uint32_t *out_count);
int nas_drm_gem_tgos_backing_page(unsigned slot,uint32_t handle,uint32_t page,uint64_t *out_pa);
int nas_drm_gem_map_offset(unsigned slot,uint32_t handle,uint64_t *out);
int nas_drm_gem_destroy(unsigned slot,uint32_t handle);
int nas_drm_gem_mmap_pin(unsigned slot,uint64_t offset,
                         struct nas_vm_device **out);
/* KMS framebuffer owns a separate object reference. Destroying the GEM
 * handle or final-closing the originating File does not invalidate scanout. */
int nas_drm_gem_pin_fb(unsigned slot,uint32_t handle,
                       struct nas_vm_device **out,uint64_t *bytes,
                       uint32_t *pitch);
int nas_drm_gem_copy_from(struct nas_vm_device *owner,uint64_t offset,
                           uint8_t *dst,uint64_t bytes);
void nas_drm_gem_file_release(unsigned slot);
#endif
