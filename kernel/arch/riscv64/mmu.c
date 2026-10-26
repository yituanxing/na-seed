#include <stdbool.h>
#include <stdint.h>
#include "../../mm/frame.h"
#include "mmu.h"
#include "../user_pt.h"

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
static uint64_t *table_at(uint64_t pa) {
    return (uint64_t *)(uintptr_t)(nas_hhdm_offset() + pa);
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
static bool table_empty(const uint64_t *p) {
    for (unsigned i = 0; i < 512; i++) if (p[i]) return false;
    return true;
}
bool nas_rv_pt_init(struct nas_rv_page_table *pt, unsigned levels) {
    if (!pt || !valid_levels(levels)) return false;
    pt->root_pa = 0;
    pt->levels = 0;
    uint64_t root = nas_frame_alloc();
    if (!good_page_pa(root)) {
        if (root) (void)nas_frame_free(root);
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
    uint64_t *tab=table_at(pt->root_pa);
    uint64_t *parents[4];uint64_t made_pa[4];unsigned made=0;
    for(unsigned level=pt->levels-1;level>0;level--) {
        uint64_t *slot=&tab[(va>>(12+9*level))&511];
        if(!*slot) {
            uint64_t pa=nas_frame_alloc();
            if(!good_page_pa(pa)) {if(pa)(void)nas_frame_free(pa);goto fail;}
            *slot=((pa>>12)<<10)|V;
            parents[made]=slot;made_pa[made]=pa;made++;
        } else if(!table_entry(*slot))goto fail;
        tab=table_at(entry_pa(*slot));
    }
    uint64_t leaf=tab[(va>>12)&511];
    if(!leaf || ((leaf&V)&&(leaf&U)&&(leaf&(R|X))&&
                  !((leaf&W)&&!(leaf&R))))return true;
fail:
    while(made) {--made;*parents[made]=0;(void)nas_frame_free(made_pa[made]);}
    return false;
}
bool nas_rv_pt_exchange(const struct nas_rv_page_table *pt,uint64_t va,
                        uint64_t new_pa,unsigned access,uint64_t *old_pa) {
    if(!old_pa || !good_user_va(pt,va) || (va&PAGE_MASK) ||
       (new_pa && (!good_page_pa(new_pa) || !(access&NAS_UP_READ) ||
                   (access&~7U))) || (!new_pa && access))return false;
    uint64_t *tab=table_at(pt->root_pa);
    for(unsigned level=pt->levels-1;level>0;level--) {
        uint64_t entry=tab[(va>>(12+9*level))&511];
        if(!table_entry(entry))return false;
        tab=table_at(entry_pa(entry));
    }
    uint64_t *leaf=&tab[(va>>12)&511];
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
    uint64_t *table = table_at(pt->root_pa);
    uint64_t *new_parent[4];
    uint64_t new_page[4];
    unsigned created = 0;
    for (unsigned level = pt->levels - 1; level > 0; level--) {
        uint64_t *slot = &table[(va >> (12 + 9 * level)) & 511];
        uint64_t child;
        if (!*slot) {
            child = nas_frame_alloc();
            if (!good_page_pa(child)) {
                if (child) (void)nas_frame_free(child);
                goto rollback;
            }
            *slot = ((child >> 12) << 10) | V;
            new_parent[created] = slot;
            new_page[created] = child;
            created++;
        } else {
            /* An existing superpage must NOT be treated as a page table. */
            if (!table_entry(*slot)) goto rollback;
            child = entry_pa(*slot);
        }
        table = table_at(child);
    }
    uint64_t *leaf = &table[(va >> 12) & 511];
    if (*leaf) goto rollback; /* forbid remap and invalid nonempty PTEs */
    *leaf = ((pa >> 12) << 10) | V | perms | A | ((perms & W) ? D : 0);
    return true;
rollback:
    while (created) {
        created--;
        *new_parent[created] = 0;
        (void)nas_frame_free(new_page[created]);
    }
    return false;
}
bool nas_rv_pt_translate(const struct nas_rv_page_table *pt, uint64_t va,
                         uint64_t *pa, uint64_t *perms) {
    if (!pa || !good_user_va(pt, va)) return false;
    uint64_t *table = table_at(pt->root_pa);
    for (unsigned level = pt->levels - 1; level > 0; level--) {
        uint64_t e = table[(va >> (12 + 9 * level)) & 511];
        if (!table_entry(e)) return false; /* no fake superpage support */
        table = table_at(entry_pa(e));
    }
    uint64_t leaf = table[(va >> 12) & 511];
    if (!(leaf & V) || !(leaf & U) ||
        !(leaf & (R | X)) || ((leaf & W) && !(leaf & R))) return false;
    *pa = entry_pa(leaf) + (va & PAGE_MASK);
    if (perms) *perms = leaf & (R | W | X | U);
    return true;
}
bool nas_rv_pt_unmap(struct nas_rv_page_table *pt, uint64_t va) {
    if (!good_user_va(pt, va) || (va & PAGE_MASK)) return false;
    uint64_t *table = table_at(pt->root_pa);
    uint64_t *parents[4];
    uint64_t children[4];
    for (unsigned level = pt->levels - 1; level > 0; level--) {
        uint64_t *slot = &table[(va >> (12 + 9 * level)) & 511];
        if (!table_entry(*slot)) return false;
        parents[level - 1] = slot;
        children[level - 1] = entry_pa(*slot);
        table = table_at(children[level - 1]);
    }
    uint64_t *leaf = &table[(va >> 12) & 511];
    if (!(*leaf & V) || !(*leaf & (R | X))) return false;
    *leaf = 0;
    /* Reclaim only empty intermediate frames, not the mapped data frame.
     * parents[] pointers reside in the higher-level tables. */
    for (unsigned idx = 0; idx < pt->levels - 1; idx++) {
        if (!table_empty(table)) break;
        if (!nas_frame_free(children[idx])) return false;
        *parents[idx] = 0;
        table = (idx + 1 == pt->levels - 1) ?
                table_at(pt->root_pa) : table_at(children[idx + 1]);
    }
    return true;
}
static void release_children(uint64_t pa, unsigned level) {
    if (!level) return;
    uint64_t *table = table_at(pa);
    for (unsigned i = 0; i < 512; i++) {
        uint64_t e = table[i];
        if (table_entry(e)) {
            uint64_t child = entry_pa(e);
            release_children(child, level - 1);
            (void)nas_frame_free(child);
        }
    }
}
void nas_rv_pt_destroy(struct nas_rv_page_table *pt) {
    if (!pt || !pt->root_pa || !valid_levels(pt->levels)) return;
    release_children(pt->root_pa, pt->levels - 1);
    (void)nas_frame_free(pt->root_pa);
    pt->root_pa = 0;
    pt->levels = 0;
}

/* Optional full-kernel primitive: change a present mapped user 4K leaf
 * without allocating/freeing frames. Caller owns TLB invalidation. */
bool nas_rv_pt_protect(const struct nas_rv_page_table *pt,uint64_t va,
                        unsigned access) {
    if (!good_user_va(pt,va) || (va & PAGE_MASK) ||
        !(access & NAS_UP_READ) || (access & ~7U)) return false;
    uint64_t *table=table_at(pt->root_pa);
    for(unsigned level=pt->levels-1;level>0;level--) {
        uint64_t e=table[(va>>(12+9*level))&511];
        if(!table_entry(e)) return false;
        table=table_at(entry_pa(e));
    }
    uint64_t *slot=&table[(va>>12)&511], e=*slot;
    if (!(e&V) || !(e&U) || !(e&(R|X))) return false;
    e &= ~(R|W|X|D);
    e |= R | ((access&NAS_UP_WRITE)?(W|D):0) |
         ((access&NAS_UP_EXEC)?X:0);
    *slot=e;
    return true;
}
static bool rv_walk(uint64_t pa,unsigned level,uint64_t base,bool top,
                    nas_user_pt_visit fn,void *ctx) {
    const uint64_t *entries=table_at(pa);
    unsigned count=top?256:512;
    for(unsigned i=0;i<count;i++) {
        uint64_t e=entries[i];
        if (!e) continue;
        if (!(e&V)) return false;
        uint64_t va=base | ((uint64_t)i<<(12+9*level));
        if (level) {
            if(!table_entry(e) ||
               !rv_walk(entry_pa(e),level-1,va,false,fn,ctx)) return false;
        } else {
            if (!(e&U) || !(e&(R|X)) || ((e&W)&&!(e&R))) return false;
            unsigned perms=((e&R)?NAS_UP_READ:0)|
                ((e&W)?NAS_UP_WRITE:0)|((e&X)?NAS_UP_EXEC:0);
            if(!fn(va,entry_pa(e),perms,ctx)) return false;
        }
    }
    return true;
}
bool nas_rv_pt_walk(const struct nas_rv_page_table *pt,
                     nas_user_pt_visit fn,void *ctx) {
    return pt && pt->root_pa && valid_levels(pt->levels) && fn &&
           rv_walk(pt->root_pa,pt->levels-1,0,true,fn,ctx);
}
