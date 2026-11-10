/* NAS-A005: actual MM+VMA invalidation and dirty re-arm over TWO live
 * address spaces. Reuse production M101 COW/fault and its physical PT mock;
 * link real vm.c rather than duplicating its truncate/invalidation rules. */
#define main m101_original_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test-m101-cow.c"
#pragma GCC diagnostic pop
#undef main
#include "../kernel/fs/vfs.h"

bool nas_fs_mmap_matches(struct nas_mapped_file *file,
                         const struct nas_fs_node *node) {
    return file==&file_owner && node && node->ino==5;
}

int main(void) {
    (void)m101_original_main();
    nas_mm_system_init();
    assert(total_refs()==0 && file_owner.refs==1);
    struct nas_mm *parent=nas_mm_new();assert(parent);
    parent->root_phys=nas_mmu_alloc_root();assert(parent->root_phys);
    assert(nas_vma_record_shared(&parent->mmap_vmas,X,2*P,3,0,&file_owner));
    assert(file_owner.refs==2);

    file_pages[0]=nas_frame_alloc();
    file_pages[1]=nas_frame_alloc();
    assert(file_pages[0] && file_pages[1]);
    memset(memory+file_pages[0],0x33,P);
    memset(memory+file_pages[1],0x72,P);
    assert(nas_frame_get(file_pages[0]) && nas_frame_get(file_pages[1]));
    uint64_t prior=0;
    assert(nas_mmu_exchange_leaf_at(parent->root_phys,X,
                                    file_pages[0],3,&prior) && !prior);
    assert(nas_mmu_exchange_leaf_at(parent->root_phys,X+P,
                                    file_pages[1],3,&prior) && !prior);
    struct nas_mm *child=nas_mm_fork_cow(parent);
    assert(child && file_owner.refs==3);
    assert(nas_frame_refs(file_pages[0])==3 &&
           nas_frame_refs(file_pages[1])==3);

    struct nas_fs_node inode={.ino=5},unrelated={.ino=6};
    unsigned perm=0;
    nas_vm_rearm_file(&unrelated);
    assert(phys(parent,X,&perm)==file_pages[0] &&
           (perm&NAS_UP_WRITE));
    nas_vm_rearm_file(&inode);
    assert(phys(parent,X,&perm)==file_pages[0] && perm==NAS_UP_READ);
    assert(phys(child,X,&perm)==file_pages[0] && perm==NAS_UP_READ);
    assert(phys(parent,X+P,&perm)==file_pages[1] && perm==NAS_UP_READ);
    assert(phys(child,X+P,&perm)==file_pages[1] && perm==NAS_UP_READ);
    assert(nas_frame_refs(file_pages[0])==3);
    unsigned dirty_before=mapped_dirty;
    assert(nas_mm_handle_fault(child,X,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED && mapped_dirty==dirty_before+1);
    assert(phys(child,X,&perm)==file_pages[0] &&
           (perm&NAS_UP_WRITE));
    assert(phys(parent,X,&perm)==file_pages[0] && perm==NAS_UP_READ);
    memory[file_pages[0]+1]=0xa7;
    assert(memory[phys(parent,X,0)+1]==0xa7);

    /* Partial EOF zeroes the same cached last-page tail in both MMs; a
     * full-page truncate revokes all other PTEs and releases their refs. */
    file_size=P+17;
    nas_vm_invalidate_file(&inode,file_size);
    assert(memory[file_pages[1]+16]==0x72 &&
           memory[file_pages[1]+17]==0 &&
           memory[file_pages[1]+P-1]==0);
    assert(phys(parent,X+P,0)==phys(child,X+P,0));
    file_size=P;
    nas_vm_invalidate_file(&inode,file_size);
    uint64_t pa=0;
    assert(!nas_mm_resolve_user(parent,X+P,&pa,0) &&
           !nas_mm_resolve_user(child,X+P,&pa,0));
    assert(nas_frame_refs(file_pages[1])==1);
    assert(nas_frame_put(file_pages[1]));file_pages[1]=0;
    assert(nas_mm_handle_fault(child,X+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_NEEDS_BACKING);
    file_size=2*P;
    assert(nas_mm_handle_fault(child,X+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(nas_mm_handle_fault(parent,X+P,NAS_UP_READ,false)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(parent,X+P,0)==phys(child,X+P,0) &&
           memory[file_pages[1]+17]==0);

    nas_mm_put(child);
    nas_mm_put(parent);
    assert(file_owner.refs==1 &&
           nas_frame_refs(file_pages[0])==1 &&
           nas_frame_refs(file_pages[1])==1);
    assert(nas_frame_put(file_pages[0]));file_pages[0]=0;
    assert(nas_frame_put(file_pages[1]));file_pages[1]=0;
    assert(total_refs()==0);
    puts("[A005 CROSS-MM HOST PASS] rearm across processes, dirty epoch, partial EOF, full invalidation, SIGBUS/regrow, refcounts");
    return 0;
}
