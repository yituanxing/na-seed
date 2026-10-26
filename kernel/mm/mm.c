#include "mm.h"
#include "../arch/mmu.h"
#include "../panic.h"
#include "../lib/string.h"
#include "../task.h"

/* Single-CPU fixed-capacity owner pool. Allocation policy is private to
 * the MM subsystem; Task must never become the storage owner again. */
static struct nas_mm mm_pool[NAS_TASK_LIMIT * 2U]; /* spare replacement MMs during exec */

void nas_mm_system_init(void) {
    memset(mm_pool, 0, sizeof(mm_pool));
}
struct nas_mm *nas_mm_new(void) {
    for (unsigned n=0; n<NAS_TASK_LIMIT * 2U; ++n) {
        struct nas_mm *mm=&mm_pool[n];
        if (mm->users) continue;
        memset(mm, 0, sizeof(*mm));
        mm->users=1;
        return mm;
    }
    return 0;
}
struct nas_mm *nas_mm_fork_eager(const struct nas_mm *source) {
    if (!source || !source->users) nas_fail("MM fork: invalid source");
    struct nas_mm *dest=nas_mm_new();
    if (!dest) return 0;
    uint64_t root=nas_mmu_fork_root(source->root_phys);
    if (!root) { nas_mm_put(dest); return 0; }
    dest->root_phys=root;
    dest->heap_break=source->heap_break;
    dest->anonymous=source->anonymous;
    return dest;
}
bool nas_mm_walk_pages(const struct nas_mm *mm,nas_mm_page_visit visit,void *ctx) {
    if(!mm || !mm->users || !mm->root_phys || !visit)return false;
    return nas_mmu_walk_user_mapping(mm->root_phys,visit,ctx);
}
bool nas_mm_resolve_user(const struct nas_mm *mm,uint64_t va,
                         uint64_t *physical,unsigned *access) {
    if(!mm || !mm->users || !mm->root_phys || !physical)return false;
    return nas_mmu_resolve_user_address(mm->root_phys,va,physical,access);
}
struct nas_mm *nas_mm_get(struct nas_mm *mm) {
    if (!mm || !mm->users || mm->users==UINT32_MAX)
        nas_fail("MM get: invalid or saturated reference");
    ++mm->users;
    return mm;
}
void nas_mm_put(struct nas_mm *mm) {
    if (!mm || !mm->users)
        nas_fail("MM put: zero or invalid reference");
    if (--mm->users) return;
    if (mm->root_phys && !nas_mmu_reclaim_root(mm->root_phys))
        nas_fail("MM final put: failed page-table reclamation");
    memset(mm, 0, sizeof(*mm));
}
