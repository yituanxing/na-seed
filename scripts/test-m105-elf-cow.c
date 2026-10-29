/* Link the real kernel/mm/mm.c + fault.c COW machinery and reuse M101's
 * frame/PT host provider. This targeted scenario tests the ELF page-union
 * logical VMA through fork -> write-fault -> preserved executable mapping.
 * The PT provider is mocked: native ISA PTE encodings need QEMU evidence. */
#define main m101_original_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test-m101-cow.c"
#pragma GCC diagnostic pop
#undef main
int main(void) {
    /* All baseline memory/refcount tests still run, then a fresh MM. */
    (void)m101_original_main();
    nas_mm_system_init();
    struct nas_mm *parent=nas_mm_new();assert(parent);
    parent->root_phys=nas_mmu_alloc_root();assert(parent->root_phys);
    assert(nas_vma_record(&parent->mmap_vmas,X,P,7,
                          NAS_VMA_ELF_PRIVATE_SNAPSHOT,0));
    map(parent,X,7,0x91);
    const uint64_t original=phys(parent,X,0);
    struct nas_task parent_task={0};
    parent_task.mm=parent;parent_task.state=NAS_TASK_RUNNING;
    nas_current_task=&parent_task;
    struct nas_mm *child=nas_mm_fork_cow(parent);assert(child);
    unsigned access=0;
    assert(phys(parent,X,&access)==original &&
           access==(NAS_UP_READ|NAS_UP_EXEC));
    assert(phys(child,X,&access)==original &&
           access==(NAS_UP_READ|NAS_UP_EXEC));
    assert(nas_vma_find(&child->mmap_vmas,X)->prot==7);
    assert(nas_mm_handle_fault(child,X,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED);
    uint64_t copied=phys(child,X,&access);
    assert(copied!=original &&
           access==(NAS_UP_READ|NAS_UP_WRITE|NAS_UP_EXEC));
    assert(memory[copied]==0x91 && memory[original]==0x91);
    assert(nas_mm_handle_fault(parent,X,NAS_UP_WRITE,true)==
           NAS_MM_FAULT_RESOLVED);
    assert(phys(parent,X,&access)==original &&
           access==(NAS_UP_READ|NAS_UP_WRITE|NAS_UP_EXEC));
    nas_mm_put(child);
    nas_mm_put(parent);
    assert(total_refs()==0);
    puts("[M105 ELF COW PASS] real MM fork/fault preserves RWX overlap boundary, frame ownership, VMA policy (mock PT)");
    return 0;
}
