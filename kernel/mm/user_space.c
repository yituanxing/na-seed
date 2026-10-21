#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "user_space.h"
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
    return nas_frame_free(pa);
}
bool nas_space_delete(struct nas_user_space *space) {
    if (!space || !space->root_pa) return false;
    /* Traverse BEFORE page-table destruction: leaves must remain readable. */
    if (!nas_arch_pt_walk(space, release_user_frame, 0)) return false;
    nas_arch_pt_destroy(space);
    return space->root_pa == 0;
}
bool nas_space_map_page(struct nas_user_space *space,
                        uint64_t va, bool writable) {
    if (!space || !page_aligned(va)) return false;
    uint64_t old_pa;
    unsigned old_access;
    if (nas_arch_pt_lookup(space, va, &old_pa, &old_access)) {
        if (!writable || (old_access & NAS_UP_WRITE)) return true;
        if (!nas_arch_pt_protect(space, va, old_access | NAS_UP_WRITE))
            return false;
        nas_arch_pt_flush(va);
        return true;
    }
    uint64_t pa = nas_frame_alloc();
    if (!pa) return false;
    /* Loader uses HHDM to populate even execute-only segments, and current
     * shared ELF loader has no RX finalization hook yet. Preserve the
     * x86 baseline's initially executable mappings until that change. */
    unsigned access = NAS_UP_READ | NAS_UP_EXEC |
                      (writable ? NAS_UP_WRITE : 0);
    if (!nas_arch_pt_map(space, va, pa, access)) {
        (void)nas_frame_free(pa);
        return false;
    }
    nas_arch_pt_flush(va);
    return true;
}
bool nas_space_unmap_page(struct nas_user_space *space, uint64_t va) {
    if (!space || !page_aligned(va)) return false;
    uint64_t pa;
    if (!nas_arch_pt_lookup(space, va, &pa, 0))
        return true; /* Preserve baseline Linux munmap of an empty page. */
    if (!nas_arch_pt_unmap(space, va)) return false;
    nas_arch_pt_flush(va);
    return nas_frame_free(pa & ~(PAGE_BYTES-1));
}
bool nas_space_protect(struct nas_user_space *space, uint64_t va,
                       uint64_t pages, unsigned prot) {
    if (!space || !page_aligned(va) || !pages || !(prot & 7U) ||
        (prot & ~7U) || pages > (UINT64_MAX-va)/PAGE_BYTES) return false;
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
struct clone_ctx { struct nas_user_space *dest; };
static bool clone_leaf(uint64_t va, uint64_t pa, unsigned access, void *opaque) {
    struct clone_ctx *ctx = (struct clone_ctx *)opaque;
    uint64_t copy_pa = nas_frame_alloc();
    if (!copy_pa) return false;
    volatile uint64_t *src = (volatile uint64_t *)(uintptr_t)(nas_hhdm_offset()+pa);
    volatile uint64_t *dst = (volatile uint64_t *)(uintptr_t)(nas_hhdm_offset()+copy_pa);
    for (unsigned n=0; n<PAGE_BYTES/8; n++) dst[n]=src[n];
    if (!nas_arch_pt_map(ctx->dest, va, copy_pa, access)) {
        (void)nas_frame_free(copy_pa);
        return false;
    }
    return true;
}
bool nas_space_clone(struct nas_user_space *dest,
                     const struct nas_user_space *source) {
    if (!dest || !source || !source->root_pa || dest == source) return false;
    if (!nas_space_new(dest, source->levels)) return false;
    struct clone_ctx ctx = { dest };
    if (nas_arch_pt_walk(source, clone_leaf, &ctx)) return true;
    if (!nas_space_delete(dest)) {
        /* An inconsistent page-table walk is a kernel invariant violation;
         * never report a successful fork with aliased/corrupt frames. */
        return false;
    }
    return false;
}
