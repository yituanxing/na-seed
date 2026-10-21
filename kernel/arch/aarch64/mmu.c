#include "mmu.h"
#include "../user_pt.h"
#include "../../mm/frame.h"

/* AArch64 stage-1 4-KiB descriptor format (not Sv39-style R/W/X bits):
 * lower levels: [1:0]=11 (table), output PA[47:12].
 * final level: [1:0]=11 (page), AttrIndx[4:2], AP[7:6],
 * SH[9:8], AF[10], PXN[53], UXN[54].
 * AttrIndx=0 assumes MAIR_EL1 index 0 is normal cacheable memory when a
 * future live backend installs TTBR0. This offline test does not assume it.
 */
#define ARM_PAGE 4096ULL
#define OFF_MASK (ARM_PAGE - 1)
#define PA_MASK 0x0000fffffffff000ULL
#define DESCRIPTOR 3ULL
#define ATTR_NORMAL 0ULL
#define AP_EL0 (1ULL << 6)
#define AP_RO  (1ULL << 7)
#define SH_INNER (3ULL << 8)
#define AF (1ULL << 10)
#define PXN (1ULL << 53)
#define UXN (1ULL << 54)
#define IDX(va, level) (((va) >> (12 + 9*(level))) & 511ULL)

static bool valid_levels(unsigned n) { return n == 3 || n == 4; }
uint64_t nas_arm_pt_user_limit(unsigned levels) {
    return valid_levels(levels) ? (1ULL << (12 + 9 * levels)) : 0;
}
static bool frame_pa(uint64_t pa) {
    return pa && !(pa & OFF_MASK) && !(pa & ~PA_MASK);
}
static bool valid_user(const struct nas_arm_pt *pt, uint64_t va) {
    return pt && frame_pa(pt->root_pa) &&
           valid_levels(pt->levels) && va < nas_arm_pt_user_limit(pt->levels);
}
static uint64_t *table(uint64_t pa) {
    return (uint64_t *)(uintptr_t)(nas_hhdm_offset() + pa);
}
static bool is_table(uint64_t desc) {
    return (desc & 3ULL) == DESCRIPTOR && (desc & PA_MASK) != 0 &&
           (desc & (AP_EL0 | AP_RO | AF | UXN | PXN | SH_INNER)) == 0;
}
static bool empty(const uint64_t *p) {
    for (unsigned i = 0; i < 512; i++) if (p[i]) return false;
    return true;
}
bool nas_arm_pt_init(struct nas_arm_pt *pt, unsigned levels) {
    if (!pt || !valid_levels(levels)) return false;
    pt->root_pa = 0; pt->levels = 0;
    uint64_t root = nas_frame_alloc();
    if (!frame_pa(root)) {
        if (root) (void)nas_frame_free(root);
        return false;
    }
    pt->root_pa = root;
    pt->levels = levels;
    return true;
}
bool nas_arm_pt_map(struct nas_arm_pt *pt, uint64_t va,
                    uint64_t pa, uint64_t access) {
    /* First user-stage contract: readable EL0 page, optional write/execute.
     * Permission-only and high VA checks are before allocating page tables.
     * W-only/executable-only mappings are not silently approximated. */
    if (!valid_user(pt, va) || (va & OFF_MASK) || !frame_pa(pa) ||
        (access & (NAS_ARM_USER | NAS_ARM_R)) != (NAS_ARM_USER | NAS_ARM_R) ||
        (access & ~(NAS_ARM_R | NAS_ARM_W | NAS_ARM_X | NAS_ARM_USER)))
        return false;
    uint64_t *parent = table(pt->root_pa);
    uint64_t *made_entry[3]; uint64_t made_pa[3]; unsigned made = 0;
    for (unsigned level = pt->levels - 1; level > 0; level--) {
        uint64_t *e = &parent[IDX(va, level)];
        uint64_t child;
        if (!*e) {
            child = nas_frame_alloc();
            if (!frame_pa(child)) {
                if (child) (void)nas_frame_free(child);
                goto rollback;
            }
            *e = child | DESCRIPTOR;
            made_entry[made] = e; made_pa[made] = child; made++;
        } else {
            /* Refuse 1GiB/2MiB block entries rather than walking into data. */
            if (!is_table(*e)) goto rollback;
            child = *e & PA_MASK;
        }
        parent = table(child);
    }
    uint64_t *leaf = &parent[IDX(va, 0)];
    if (*leaf) goto rollback;
    *leaf = pa | DESCRIPTOR | ATTR_NORMAL | AP_EL0 | SH_INNER | AF | PXN |
            ((access & NAS_ARM_W) ? 0 : AP_RO) |
            ((access & NAS_ARM_X) ? 0 : UXN);
    return true;
rollback:
    while (made) {
        made--;
        *made_entry[made] = 0;
        (void)nas_frame_free(made_pa[made]);
    }
    return false;
}
bool nas_arm_pt_translate(const struct nas_arm_pt *pt, uint64_t va,
                          uint64_t *pa, uint64_t *access) {
    if (!pa || !valid_user(pt, va)) return false;
    uint64_t *p = table(pt->root_pa);
    for (unsigned level = pt->levels - 1; level > 0; level--) {
        uint64_t e = p[IDX(va, level)];
        if (!is_table(e)) return false;
        p = table(e & PA_MASK);
    }
    uint64_t e = p[IDX(va, 0)];
    if ((e & 3) != DESCRIPTOR || !(e & AF) || !(e & AP_EL0) ||
        !(e & PA_MASK)) return false;
    *pa = (e & PA_MASK) + (va & OFF_MASK);
    if (access) {
        *access = NAS_ARM_USER | NAS_ARM_R |
                  ((e & AP_RO) ? 0 : NAS_ARM_W) |
                  ((e & UXN) ? 0 : NAS_ARM_X);
    }
    return true;
}
bool nas_arm_pt_unmap(struct nas_arm_pt *pt, uint64_t va) {
    if (!valid_user(pt, va) || (va & OFF_MASK)) return false;
    uint64_t *p = table(pt->root_pa);
    uint64_t *ancestors[3]; uint64_t frames[3];
    for (unsigned level = pt->levels - 1; level > 0; level--) {
        uint64_t *e = &p[IDX(va, level)];
        if (!is_table(*e)) return false;
        ancestors[level - 1] = e;
        frames[level - 1] = *e & PA_MASK;
        p = table(frames[level - 1]);
    }
    uint64_t *leaf = &p[IDX(va, 0)];
    if ((*leaf & 3) != DESCRIPTOR || !(*leaf & AP_EL0)) return false;
    *leaf = 0;
    for (unsigned idx = 0; idx < pt->levels - 1; idx++) {
        if (!empty(p)) break;
        if (!nas_frame_free(frames[idx])) return false;
        *ancestors[idx] = 0;
        p = (idx + 1 == pt->levels - 1) ?
            table(pt->root_pa) : table(frames[idx + 1]);
    }
    return true;
}
static void destroy_descendants(uint64_t pa, unsigned levels_down) {
    if (!levels_down) return;
    uint64_t *p = table(pa);
    for (unsigned i = 0; i < 512; i++) {
        if (is_table(p[i])) {
            uint64_t child = p[i] & PA_MASK;
            destroy_descendants(child, levels_down - 1);
            (void)nas_frame_free(child);
        }
    }
}
void nas_arm_pt_destroy(struct nas_arm_pt *pt) {
    if (!pt || !frame_pa(pt->root_pa) || !valid_levels(pt->levels)) return;
    destroy_descendants(pt->root_pa, pt->levels - 1);
    (void)nas_frame_free(pt->root_pa);
    pt->root_pa = 0; pt->levels = 0;
}

bool nas_arm_pt_protect(const struct nas_arm_pt *pt,uint64_t va,
                         unsigned access) {
    if (!valid_user(pt,va) || (va & OFF_MASK) ||
        !(access & NAS_UP_READ) || (access & ~7U)) return false;
    uint64_t *tablep=table(pt->root_pa);
    for (unsigned level=pt->levels-1;level>0;level--) {
        uint64_t e=tablep[IDX(va,level)];
        if(!is_table(e)) return false;
        tablep=table(e&PA_MASK);
    }
    uint64_t *leaf=&tablep[IDX(va,0)];
    uint64_t e=*leaf;
    if ((e&3)!=DESCRIPTOR || !(e&AP_EL0) || !(e&AF)) return false;
    e &= ~(AP_RO|UXN);
    if (!(access&NAS_UP_WRITE)) e |= AP_RO;
    if (!(access&NAS_UP_EXEC)) e |= UXN;
    *leaf=e;
    return true;
}
static bool arm_walk(uint64_t pa,unsigned level,uint64_t base,bool top,
                     nas_user_pt_visit fn,void *ctx) {
    const uint64_t *entries=table(pa);
    unsigned count=top?256:512;
    for(unsigned i=0;i<count;i++) {
        uint64_t e=entries[i];
        if(!e) continue;
        uint64_t va=base | ((uint64_t)i<<(12+9*level));
        if(level) {
            if(!is_table(e) ||
               !arm_walk(e&PA_MASK,level-1,va,false,fn,ctx)) return false;
        } else {
            if((e&3)!=DESCRIPTOR || !(e&AP_EL0) || !(e&AF) ||
               !(e&PA_MASK)) return false;
            unsigned perms=NAS_UP_READ |
                ((e&AP_RO)?0:NAS_UP_WRITE) |
                ((e&UXN)?0:NAS_UP_EXEC);
            if(!fn(va,e&PA_MASK,perms,ctx)) return false;
        }
    }
    return true;
}
bool nas_arm_pt_walk(const struct nas_arm_pt *pt,
                      nas_user_pt_visit fn,void *ctx) {
    return pt && frame_pa(pt->root_pa) && valid_levels(pt->levels) && fn &&
           arm_walk(pt->root_pa,pt->levels-1,0,true,fn,ctx);
}
