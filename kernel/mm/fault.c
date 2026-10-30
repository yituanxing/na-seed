#include <stdint.h>
#include "fault.h"
#include "mm.h"
#include "frame.h"
#include "../arch/mmu.h"
#include "../arch/user_pt.h"
#include "prot.h"
/* Private anonymous mmap pages are demand-zero and initially nonresident.
 * This provider resolves absent anonymous/heap/stack pages whose VMA
 * logically permits access, including kernel usercopy's fault-in path.
 * File/private snapshot faults MUST NOT be replaced with zero-filled data;
 * write-protected resident pages MUST NOT have COW bypassed. */
enum nas_mm_fault_result nas_mm_handle_fault(struct nas_mm *mm,uint64_t addr,
                                             unsigned access,bool hardware_protection_fault) {
    if(!mm || !mm->users || !mm->root_phys || addr>=USER_LIMIT ||
       !(access&(NAS_UP_READ|NAS_UP_WRITE|NAS_UP_EXEC)) ||
       (access&~(NAS_UP_READ|NAS_UP_WRITE|NAS_UP_EXEC)))
        return NAS_MM_FAULT_MAPERR;
    const struct nas_anon_region *vma=nas_vma_find(&mm->mmap_vmas,addr);
    if(!vma)return NAS_MM_FAULT_MAPERR;
    if(!vma->prot ||
       ((access&NAS_UP_READ) && !(vma->prot&NAS_VM_READ)) ||
       ((access&NAS_UP_WRITE) && !(vma->prot&NAS_VM_WRITE)) ||
       ((access&NAS_UP_EXEC) && !(vma->prot&NAS_VM_EXEC)))
        return NAS_MM_FAULT_ACCERR;
    uint64_t pa=0;
    unsigned hw=0;
    if(nas_mm_resolve_user(mm,addr,&pa,&hw)) {
        if((access&hw)!=access) {
            /* Only a logically writable private VMA can resolve a write-
             * protected resident page. Physical refcount decides whether
             * to copy or upgrade the sole remaining owner's leaf. */
            if(!(access&NAS_UP_WRITE) || (access&NAS_UP_EXEC))
                return NAS_MM_FAULT_ACCERR;
            return nas_mm_break_cow(mm,addr,vma->prot,false) ?
                NAS_MM_FAULT_RESOLVED : NAS_MM_FAULT_OOM;
        }
        return NAS_MM_FAULT_INVARIANT; /* HW fault with usable user PTE */
    }
    if(hardware_protection_fault)return NAS_MM_FAULT_INVARIANT;
    if(vma->kind!=NAS_VMA_ANON_PRIVATE &&
       vma->kind!=NAS_VMA_HEAP && vma->kind!=NAS_VMA_STACK)
        return NAS_MM_FAULT_NEEDS_BACKING;
    /* Zeroing PMM frame is staged before page-table publication. */
    uint64_t fresh=nas_frame_alloc();
    if(!fresh)return NAS_MM_FAULT_OOM;
    uint64_t va=addr & ~UINT64_C(4095);
    if(!nas_mmu_prepare_leaf_at(mm->root_phys,va)){
        (void)nas_frame_put(fresh);
        return NAS_MM_FAULT_OOM;
    }
    uint64_t old=0;
    if(!nas_mmu_exchange_leaf_at(mm->root_phys,va,fresh,vma->prot,&old)){
        (void)nas_frame_put(fresh);
        return NAS_MM_FAULT_INVARIANT;
    }
    if(old)__builtin_trap(); /* preflight proved absent; do not leak owners */
    return NAS_MM_FAULT_RESOLVED;
}
