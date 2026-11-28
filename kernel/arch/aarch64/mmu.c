#include "mmu.h"
#include "../user_pt.h"
#include "../pt_directory.h"
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
#define ATTR_NORMAL (7ULL << 2) /* user normal WB MAIR index 7 */
#define AP_EL0 (1ULL << 6)
#define AP_RO  (1ULL << 7)
#define SH_INNER (3ULL << 8)
#define AF (1ULL << 10)
#define PXN (1ULL << 53)
#define UXN (1ULL << 54)

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
static bool is_table(uint64_t desc) {
    return (desc & 3ULL) == DESCRIPTOR && (desc & PA_MASK) != 0 &&
           (desc & (AP_EL0 | AP_RO | AF | UXN | PXN | SH_INNER)) == 0;
}
static uint64_t entry_pa(uint64_t e) { return e&PA_MASK; }
static uint64_t directory_entry(uint64_t pa) { return pa|DESCRIPTOR; }
static const struct nas_pt_dir_ops dirs={
    .valid_pa=frame_pa,.is_table=is_table,
    .decode=entry_pa,.encode=directory_entry
};
bool nas_arm_pt_init(struct nas_arm_pt *pt, unsigned levels) {
    if (!pt || !valid_levels(levels)) return false;
    pt->root_pa = 0; pt->levels = 0;
    uint64_t root = nas_frame_alloc();
    if (!frame_pa(root)) {
        if (root) (void)nas_frame_put(root);
        return false;
    }
    pt->root_pa = root;
    pt->levels = levels;
    return true;
}

bool nas_arm_pt_prepare(const struct nas_arm_pt *pt,uint64_t va) {
    if(!valid_user(pt,va) || (va&OFF_MASK))return false;
    struct nas_pt_dir_path path={0};
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,&path,true);
    if(!leaf)return false;
    uint64_t e=*leaf;
    if(!e || ((e&3)==DESCRIPTOR && (e&AF) && (e&AP_EL0)))return true;
    nas_pt_dir_rollback(&path);
    return false;
}
bool nas_arm_pt_exchange(const struct nas_arm_pt *pt,uint64_t va,
                         uint64_t new_pa,unsigned access,uint64_t *old_pa) {
    if(!old_pa || !valid_user(pt,va) || (va&OFF_MASK) ||
       (new_pa && (!frame_pa(new_pa) || !(access&NAS_UP_READ) ||
                   (access&~7U))) || (!new_pa && access))return false;
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,0,false);
    if(!leaf)return false;
    uint64_t prev=*leaf;
    if(prev && ((prev&3)!=DESCRIPTOR || !(prev&AF) ||
                !(prev&AP_EL0)))return false;
    *leaf=new_pa ? new_pa|DESCRIPTOR|ATTR_NORMAL|AP_EL0|SH_INNER|AF|PXN|
          ((access&NAS_UP_WRITE)?0:AP_RO)|
          ((access&NAS_UP_EXEC)?0:UXN) : 0;
    *old_pa=prev?(prev&PA_MASK):0;
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
    struct nas_pt_dir_path path={0};
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,&path,true);
    if(!leaf)return false;
    if(*leaf) {
        nas_pt_dir_rollback(&path);
        return false;
    }
    *leaf = pa | DESCRIPTOR | ATTR_NORMAL | AP_EL0 | SH_INNER | AF | PXN |
            ((access & NAS_ARM_W) ? 0 : AP_RO) |
            ((access & NAS_ARM_X) ? 0 : UXN);
    return true;
}
bool nas_arm_pt_translate(const struct nas_arm_pt *pt, uint64_t va,
                          uint64_t *pa, uint64_t *access) {
    if (!pa || !valid_user(pt, va)) return false;
    uint64_t *slot=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,0,false);
    if(!slot)return false;
    uint64_t e=*slot;
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
    struct nas_pt_dir_path path={0};
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,&path,false);
    if(!leaf)return false;
    if ((*leaf & 3) != DESCRIPTOR || !(*leaf & AP_EL0)) return false;
    *leaf=0;
    return nas_pt_dir_reclaim(pt->levels,&path);
}
void nas_arm_pt_destroy(struct nas_arm_pt *pt) {
    if (!pt || !frame_pa(pt->root_pa) || !valid_levels(pt->levels)) return;
    nas_pt_dir_destroy(&dirs,pt->root_pa,pt->levels-1);
    (void)nas_frame_put(pt->root_pa);
    pt->root_pa = 0; pt->levels = 0;
}

bool nas_arm_pt_protect(const struct nas_arm_pt *pt,uint64_t va,
                         unsigned access) {
    if (!valid_user(pt,va) || (va & OFF_MASK) ||
        !(access & NAS_UP_READ) || (access & ~7U)) return false;
    uint64_t *leaf=nas_pt_dir_leaf(&dirs,pt->root_pa,pt->levels,va,0,false);
    if(!leaf)return false;
    uint64_t e=*leaf;
    if ((e&3)!=DESCRIPTOR || !(e&AP_EL0) || !(e&AF)) return false;
    e &= ~(AP_RO|UXN);
    if (!(access&NAS_UP_WRITE)) e |= AP_RO;
    if (!(access&NAS_UP_EXEC)) e |= UXN;
    *leaf=e;
    return true;
}
static bool arm_leaf_access(uint64_t e,unsigned *perms) {
    if((e&3)!=DESCRIPTOR || !(e&AP_EL0) || !(e&AF) || !(e&PA_MASK))
        return false;
    *perms=NAS_UP_READ | ((e&AP_RO)?0:NAS_UP_WRITE) |
        ((e&UXN)?0:NAS_UP_EXEC);
    return true;
}
bool nas_arm_pt_walk(const struct nas_arm_pt *pt,
                      nas_user_pt_visit fn,void *ctx) {
    return pt && frame_pa(pt->root_pa) && valid_levels(pt->levels) && fn &&
           nas_pt_dir_walk(&dirs,pt->root_pa,pt->levels-1,0,512,
                           arm_leaf_access,fn,ctx);
}
