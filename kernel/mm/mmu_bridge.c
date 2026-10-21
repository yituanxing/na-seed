#include <stdint.h>
#include <stdbool.h>
#include "user_space.h"
#include "../task.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#if defined(__x86_64__)
#error "Use proven native x86 MMU; this bridge is for RV64 and AArch64."
#endif
/* Same API already consumed by existing ELF/exec/FS/fork/usercopy modules.
 * root_phys is per task; levels is derived from the actual boot CPU mode.
 * No alternate miniature kernel, new ELF implementation or FS. */
static struct nas_user_space current(void) {
    return (struct nas_user_space){nas_current_task->root_phys,
                                  nas_arch_pt_active_levels()};
}
bool nas_mmu_bootstrap(void) {
    struct nas_user_space s={0};
    if (!nas_current_task || !nas_space_new(&s,nas_arch_pt_active_levels()))
        return false;
    nas_current_task->root_phys=s.root_pa;
    return nas_arch_pt_install(&s);
}
uint64_t nas_mmu_alloc_root(void) {
    struct nas_user_space s={0};
    return nas_space_new(&s,nas_arch_pt_active_levels())?s.root_pa:0;
}
bool nas_mmu_reclaim_root(uint64_t pa) {
    struct nas_user_space s={pa,nas_arch_pt_active_levels()};
    return nas_space_delete(&s);
}
void nas_mmu_switch_root(uint64_t pa) {
    struct nas_user_space s={pa,nas_arch_pt_active_levels()};
    if (!nas_arch_pt_install(&s)) nas_arch_halt();
    nas_current_task->root_phys=pa;
}
uint64_t nas_mmu_fork_root(void) {
    struct nas_user_space source=current(), dest={0};
    return nas_space_clone(&dest,&source)?dest.root_pa:0;
}
bool nas_mmu_map_page(uint64_t va,bool writable) {
    struct nas_user_space s=current();
    return nas_space_map_page(&s,va,writable);
}
bool nas_mmu_unmap_page(uint64_t va) {
    struct nas_user_space s=current();
    return nas_space_unmap_page(&s,va);
}
uint8_t *nas_mmu_user_byte(uint64_t va) {
    struct nas_user_space s=current();
    return nas_space_user_byte(&s,va);
}
bool nas_mmu_user_writable(uint64_t va) {
    struct nas_user_space s=current();
    return nas_space_user_writable(&s,va);
}
bool nas_mmu_protect_pages(uint64_t va,uint64_t pages,uint64_t prot) {
    struct nas_user_space s=current();
    return nas_space_protect(&s,va,pages,(unsigned)prot);
}
