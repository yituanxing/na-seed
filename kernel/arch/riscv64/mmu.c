#include <stdbool.h>
#include <stdint.h>
#include "../../mm/frame.h"
#include "mmu.h"
#include "../user_pt.h"
#include "../pt_directory.h"

/* RISC-V privileged ISA PTE encoding: low 10 flags, then PPN[43:0].
 * V-only intermediate entries, 4-KiB R/W/X/U/A/D leaf entries.
 * Sv39/48/57 differ solely in the number of 9-bit directory indices.
 * Hardware-superpage splitting, COW, ASIDs and TLB shootdown are future
 * features; reject huge leaves instead of silently corrupting them. */
#define V 1ULL
#define R NAS_RV_PTE_R
#define W NAS_RV_PTE_W
#define X NAS_RV_PTE_X
#define U NAS_RV_PTE_U
#define A (1ULL << 6)
#define D (1ULL << 7)
#define PPN_MASK ((1ULL << 44) - 1)
#define PTE_PPN_MASK (PPN_MASK << 10)
#define PAGE_MASK (NAS_RV_PAGE_SIZE - 1)

static bool valid_levels(unsigned levels) {
    return levels >= 3 && levels <= 5;
}
uint64_t nas_rv_pt_user_limit(unsigned levels) {
    if (!valid_levels(levels)) return 0;
    return 1ULL << (11 + levels * 9); /* 2^(VA_bits-1) */
}
static uint64_t entry_pa(uint64_t pte) {
    return ((pte >> 10) & PPN_MASK) << 12;
}
static bool table_entry(uint64_t pte) {
    /* Nonleaf U/A/D and software flags are reserved for this first backend;
     * a leaf has at least one of R/X. PTE W without R is illegal. */
    return (pte & V) && !(pte & (R | W | X | U | A | D |
                                (1ULL << 5) | (3ULL << 8)));
}
static bool good_page_pa(uint64_t pa) {
    return pa && !(pa & PAGE_MASK) && (pa >> 12) <= PPN_MASK;
}
static bool good_user_va(const struct nas_rv_page_table *pt, uint64_t va) {
    return pt && pt->root_pa && valid_levels(pt->levels) &&
           va < nas_rv_pt_user_limit(pt->levels);
}
static uint64_t directory_entry(uint64_t pa) { return ((pa>>12)<<10)|V; }
static const struct nas_pt_dir_ops dirs={
    .valid_pa=good_page_pa,.is_table=table_entry,
    .decode=entry_pa,.encode=directory_entry
};
bool nas_rv_pt_init(struct nas_rv_page_table *pt, unsigned levels) {
    if (!pt || !valid_levels(levels)) return false;
    pt->root_pa = 0;
    pt->levels = 0;
    uint64_t root = nas_frame_alloc();
    if (!good_page_pa(root)) {
        if (root) (void)nas_frame_put(root);
        return false;
    }
    pt->root_pa = root;
    pt->levels = levels;
    return true;
}

/* Precreate only directory frames. Roll back freshly allocated directories
 * if preparation fails; do not touch existing leaf entries or data frames. */
bool nas_rv_pt_prepare(const struct nas_rv_page_table *pt,uint64_t va) {
    if(!good_user_va(pt,va) || (va&PAGE_MASK))return false;
    struct nas_pt_dir_path path={0};
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,&path,true);
    if(!leaf)return false;
    uint64_t e=*leaf;
    if(!e || ((e&V)&&(e&U)&&(e&(R|X))&&!((e&W)&&!(e&R))))return true;
    nas_pt_dir_rollback(&path);
    return false;
}
bool nas_rv_pt_exchange(const struct nas_rv_page_table *pt,uint64_t va,
                        uint64_t new_pa,unsigned access,uint64_t *old_pa) {
    if(!old_pa || !good_user_va(pt,va) || (va&PAGE_MASK) ||
       (new_pa && (!good_page_pa(new_pa) || !(access&NAS_UP_READ) ||
                   (access&~7U))) || (!new_pa && access))return false;
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,0,false);
    if(!leaf)return false;
    uint64_t prev=*leaf;
    if(prev && (!(prev&V)||!(prev&U)||!(prev&(R|X))))return false;
    uint64_t flags=U|R|((access&NAS_UP_WRITE)?W:0)|
                   ((access&NAS_UP_EXEC)?X:0);
    *leaf=new_pa ? ((new_pa>>12)<<10)|V|flags|A|
            ((flags&W)?D:0) : 0;
    *old_pa=prev?entry_pa(prev):0;
    return true;
}
bool nas_rv_pt_map(struct nas_rv_page_table *pt, uint64_t va,
                   uint64_t pa, uint64_t perms) {
    if (!good_user_va(pt, va) || (va & PAGE_MASK) ||
        !good_page_pa(pa) || !(perms & U) ||
        !(perms & (R | X)) || ((perms & W) && !(perms & R)) ||
        (perms & ~(R | W | X | U))) return false;
    struct nas_pt_dir_path path={0};
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,&path,true);
    if(!leaf)return false;
    if(*leaf) {
        nas_pt_dir_rollback(&path);
        return false;
    }
    *leaf = ((pa >> 12) << 10) | V | perms | A | ((perms & W) ? D : 0);
    return true;
}
bool nas_rv_pt_translate(const struct nas_rv_page_table *pt, uint64_t va,
                         uint64_t *pa, uint64_t *perms) {
    if (!pa || !good_user_va(pt, va)) return false;
    uint64_t *slot=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,0,false);
    if(!slot)return false;
    uint64_t leaf=*slot;
    if (!(leaf & V) || !(leaf & U) ||
        !(leaf & (R | X)) || ((leaf & W) && !(leaf & R))) return false;
    *pa = entry_pa(leaf) + (va & PAGE_MASK);
    if (perms) *perms = leaf & (R | W | X | U);
    return true;
}
bool nas_rv_pt_unmap(struct nas_rv_page_table *pt, uint64_t va) {
    if (!good_user_va(pt, va) || (va & PAGE_MASK)) return false;
    struct nas_pt_dir_path path={0};
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,&path,false);
    if(!leaf)return false;
    if (!(*leaf & V) || !(*leaf & (R | X))) return false;
    *leaf=0;
    return nas_pt_dir_reclaim(pt->levels,&path);
}
void nas_rv_pt_destroy(struct nas_rv_page_table *pt) {
    if (!pt || !pt->root_pa || !valid_levels(pt->levels)) return;
    nas_pt_dir_destroy(&dirs,pt->root_pa,pt->levels-1);
    (void)nas_frame_put(pt->root_pa);
    pt->root_pa = 0;
    pt->levels = 0;
}

/* Optional full-kernel primitive: change a present mapped user 4K leaf
 * without allocating/freeing frames. Caller owns TLB invalidation. */
bool nas_rv_pt_protect(const struct nas_rv_page_table *pt,uint64_t va,
                        unsigned access) {
    if (!good_user_va(pt,va) || (va & PAGE_MASK) ||
        !(access & NAS_UP_READ) || (access & ~7U)) return false;
    uint64_t *slot=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,0,false);
    if(!slot)return false;
    uint64_t e=*slot;
    if (!(e&V) || !(e&U) || !(e&(R|X))) return false;
    e &= ~(R|W|X|D);
    e |= R | ((access&NAS_UP_WRITE)?(W|D):0) |
         ((access&NAS_UP_EXEC)?X:0);
    *slot=e;
    return true;
}
static bool rv_leaf_access(uint64_t e,unsigned *perms) {
    if(!(e&V) || !(e&U) || !(e&(R|X)) || ((e&W)&&!(e&R)))return false;
    *perms=((e&R)?NAS_UP_READ:0) | ((e&W)?NAS_UP_WRITE:0) |
        ((e&X)?NAS_UP_EXEC:0);
    return true;
}
bool nas_rv_pt_walk(const struct nas_rv_page_table *pt,
                     nas_user_pt_visit fn,void *ctx) {
    return pt && pt->root_pa && valid_levels(pt->levels) && fn &&
           nas_pt_dir_walk(&dirs,pt->root_pa,pt->levels-1,0,256,
                           rv_leaf_access,fn,ctx);
}
