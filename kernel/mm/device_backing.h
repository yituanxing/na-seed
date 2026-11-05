#ifndef NASEED_MM_DEVICE_BACKING_H
#define NASEED_MM_DEVICE_BACKING_H
#include <stdbool.h>
#include <stdint.h>

/* A stable device mapping identity, independent of the fd/OpenFile that
 * created a VMA. The provider owns this descriptor and the physical pages.
 * The MM owns one get()/put() pin per live VMA slot, including fragments,
 * speculative metadata clones, and child address spaces after fork.
 *
 * page() reports a PAGE-aligned, PMM-managed CPU physical frame covering
 * the requested byte offset. This is a BORROW: the caller must acquire an
 * independent nas_frame_get() reference before publishing a user PTE, and
 * may subsequently release that PTE with the ordinary nas_frame_put() path.
 * DMA bus addresses, firmware-reserved pages and unmanaged MMIO are NOT
 * valid page() results. Providers lacking this guarantee must reject mmap.
 *
 * Callbacks never receive user pointers or process-local fd numbers.
 * A successful get() keeps the descriptor and all mapped physical pages
 * alive until the matching put(), even after the originating fd is closed.
 */
struct nas_vm_device {
    void *context;
    uint64_t extent_bytes; /* valid logical surface; final PMM page may be partial */
    bool (*get)(void *context);
    void (*put)(void *context);
    int (*page)(void *context,uint64_t byte_offset,uint64_t *frame_pa);
};

#endif
