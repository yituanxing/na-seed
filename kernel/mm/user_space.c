#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "user_space.h"
#include "prot.h"
#define PAGE_BYTES 4096ULL
/* Arch page tables never own user DATA frames: one shared owner controls
 * mapping, cloning, rollback and reclamation. Reject malformed walks. */
static bool page_aligned(uint64_t va) { return va && !(va & (PAGE_BYTES-1)); }
static struct nas_user_space fresh(void) {
    return (struct nas_user_space){0};
}
bool nas_space_new(struct nas_user_space *space, unsigned levels) {
    if (!space) return false;
    *space = fresh();
    return nas_arch_pt_new(space, levels);
}
bool nas_space_lookup(const struct nas_user_space *space, uint64_t va,
                      uint64_t *pa, unsigned *access) {
    return nas_arch_pt_lookup(space, va, pa, access);
}
static bool release_user_frame(uint64_t va, uint64_t pa,
                               unsigned permissions, void *ctx) {
    (void)va; (void)permissions; (void)ctx;
    return nas_frame_put(pa);
}
bool nas_space_delete(struct nas_user_space *space) {
    if (!space || !space->root_pa) return false;
    /* Traverse BEFORE page-table destruction: leaves must remain readable. */
    if (!nas_arch_pt_walk(space, release_user_frame, 0)) return false;
    nas_arch_pt_destroy(space);
    return space->root_pa == 0;
}
bool nas_space_map_access(struct nas_user_space *space,uint64_t va,
                          unsigned access) {
    if(!space || !page_aligned(va) || !(access&NAS_UP_READ) ||
       (access&~7U) || ((access&NAS_UP_WRITE)&&(access&NAS_UP_EXEC)))
        return false;
    uint64_t prior;
    if(nas_arch_pt_lookup(space,va,&prior,0)) return false;
    uint64_t pa=nas_frame_alloc();
    if(!pa) return false;
    if(!nas_arch_pt_map(space,va,pa,access)) {
        (void)nas_frame_put(pa);
        return false;
    }
    nas_arch_pt_flush(va);
    return true;
}
bool nas_space_map_page(struct nas_user_space *space,
                        uint64_t va, bool writable) {
    if (!space || !page_aligned(va)) return false;
    uint64_t old_pa;
    unsigned old_access;
    if (nas_arch_pt_lookup(space, va, &old_pa, &old_access)) {
        if (!writable || (old_access & NAS_UP_WRITE)) return true;
        /* Never promote executable memory to W+X by an untyped API. */
        if (old_access & NAS_UP_EXEC) return false;
        if (!nas_arch_pt_protect(space, va, old_access | NAS_UP_WRITE))
            return false;
        nas_arch_pt_flush(va);
        return true;
    }
    return nas_space_map_access(space,va,NAS_UP_READ|
                               (writable?NAS_UP_WRITE:NAS_UP_EXEC));
}
bool nas_space_unmap_page(struct nas_user_space *space, uint64_t va) {
    if (!space || !page_aligned(va)) return false;
    uint64_t pa;
    if (!nas_arch_pt_lookup(space, va, &pa, 0))
        return true; /* Preserve baseline Linux munmap of an empty page. */
    if (!nas_arch_pt_unmap(space, va)) return false;
    nas_arch_pt_flush(va);
    return nas_frame_put(pa & ~(PAGE_BYTES-1));
}
bool nas_space_protect(struct nas_user_space *space, uint64_t va,
                       uint64_t pages, unsigned prot) {
    if (!space || !page_aligned(va) || !pages || !nas_vm_prot_supported(prot) ||
        pages > (UINT64_MAX-va)/PAGE_BYTES) return false;
    unsigned desired = (prot & 7U) | NAS_UP_READ;
    /* Validate entire range first to avoid partially changing permissions
     * on ordinary unmapped-range failures. */
    for (uint64_t i=0; i<pages; i++)
        if (!nas_arch_pt_lookup(space, va+i*PAGE_BYTES, 0, 0)) return false;
    for (uint64_t i=0; i<pages; i++) {
        uint64_t page = va+i*PAGE_BYTES;
        if (!nas_arch_pt_protect(space, page, desired)) return false;
        nas_arch_pt_flush(page);
    }
    return true;
}
bool nas_space_user_writable(const struct nas_user_space *space, uint64_t va) {
    unsigned prot;
    return nas_arch_pt_lookup(space, va, 0, &prot) &&
           (prot & NAS_UP_WRITE) != 0;
}
uint8_t *nas_space_user_byte(const struct nas_user_space *space, uint64_t va) {
    uint64_t pa;
    if (!nas_arch_pt_lookup(space, va, &pa, 0)) return 0;
    return (uint8_t *)(uintptr_t)(nas_hhdm_offset() + pa);
}
