/* M27: host-side ownership / fork / reuse tests for actual kernel/task.c.
 * The real stock BusyBox regression separately tests live QEMU transitions. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/task.h"

/* Exercise the real MM ownership implementation. Only hardware page-table
 * primitives and FD bootstrap are replaced in this CPU-independent test. */
void nas_print(const char *msg) { fputs(msg, stderr); }
__attribute__((noreturn)) void nas_arch_halt(void) { abort(); }
static uint64_t fork_root=0x3000;
static unsigned reclaimed;
static uint64_t seen_source_root;
static uint64_t cloned_physical;
static unsigned frame_owners=1;
#define TEST_USER_VA UINT64_C(0x20000000)
uint64_t nas_mmu_alloc_root(void){return fork_root;}
uint64_t nas_frame_alloc(void){return 0;} /* no eager copy for RO user page */
uint64_t nas_hhdm_offset(void){return 0;} /* no private data copy in fixture */
bool nas_frame_get(uint64_t pa) {
    if(pa!=0x9000)return false;
    ++frame_owners;return true;
}
bool nas_frame_put(uint64_t pa) {
    if(pa!=0x9000 || frame_owners<=1)return false;
    --frame_owners;return true;
}
uint32_t nas_frame_refs(uint64_t pa) {
    return pa==0x9000?frame_owners:0;
}
bool nas_mmu_prepare_leaf_at(uint64_t root,uint64_t va) {
    return root==fork_root && va==TEST_USER_VA;
}
bool nas_mmu_exchange_leaf_at(uint64_t root,uint64_t va,uint64_t pa,
                              unsigned prot,uint64_t *old_pa) {
    if(root!=fork_root || va!=TEST_USER_VA || pa!=0x9000 ||
       prot!=1 || !old_pa)return false;
    *old_pa=cloned_physical;
    cloned_physical=pa;
    return true;
}
bool nas_mmu_resolve_user_address(uint64_t source_root,uint64_t va,
                                  uint64_t *physical,unsigned *access) {
    if(!physical || (source_root!=0x2000 && source_root!=fork_root) ||
       (va&~4095ULL)!=TEST_USER_VA || (source_root==fork_root && !cloned_physical))
        return false;
    *physical=0x9000+(va&4095ULL);
    if(access)*access=1;
    return true;
}
bool nas_mmu_walk_user_mapping(uint64_t source_root,
                               bool (*visit)(uint64_t,unsigned,void *),void *ctx) {
    seen_source_root=source_root;
    return source_root==0x2000 && visit(TEST_USER_VA,1,ctx);
}
static bool see_page(uint64_t va,unsigned prot,void *opaque) {
    uint64_t *count=opaque;
    if(va!=TEST_USER_VA || prot!=1)return false;
    ++*count;
    return true;
}
bool nas_mmu_reclaim_root(uint64_t root) {
    if (root!=fork_root) abort();
    if(cloned_physical && !nas_frame_put(cloned_physical))abort();
    cloned_physical=0;
    reclaimed++;
    return true;
}
static void ck(int condition, const char *why) {
    if (!condition) { fprintf(stderr,"[TASK STACK FAIL] %s\n",why); exit(1); }
}
int main(void) {
    nas_task_boot_init();
    struct nas_task *init=nas_task_current();
    ck(init==nas_task_at(0) && init->pid==1,"PID1 owner");
    ck(init->mm && init->mm->users==1,"init owns a live independent MM");
    ck(init->files && init->files->refs==1 && init->fds==&init->files->table,
       "init owns independent files object");
    ck(init->fs && init->fs->refs==1 && init->sighand && init->sighand->refs==1,
       "init owns independent FS and signal resources");
    init->fs->umask=027U;
    init->sighand->actions[10][0]=123U;
    nas_mm_get(init->mm);
    ck(init->mm->users==2,"MM get adds one owner");
    nas_mm_put(init->mm);
    ck(init->mm->users==1,"MM put preserves active owner");
    init->mm->root_phys=0x2000;
    uint64_t pa=0,count=0;
    unsigned prot=0;
    ck(nas_mm_resolve_user(init->mm,TEST_USER_VA+8,&pa,&prot) &&
       pa==0x9008 && prot==1,"MM explicit lookup selects its own root");
    ck(nas_mm_walk_pages(init->mm,see_page,&count) && count==1,
       "MM page walk is independent of Task leader");
    ck(!nas_mm_resolve_user(init->mm,TEST_USER_VA+4096,&pa,&prot),
       "MM cannot claim an absent PTE");
    init->mm->heap_break=0x12345000ULL;
    /* This test's physical leaf must have the SAME logical VMA owner as
     * production ELF/heap/stack mappings. Never exercise orphan-PTE fork. */
    ck(nas_vma_record(&init->mm->mmap_vmas,TEST_USER_VA,4096,1,
                      NAS_VMA_ANON_PRIVATE,0),"test leaf has VMA owner");
    struct nas_mm *forked_mm=nas_mm_fork_cow(init->mm);
    ck(forked_mm && forked_mm!=init->mm &&
       seen_source_root==init->mm->root_phys &&
       forked_mm->root_phys==fork_root &&
       forked_mm->heap_break==init->mm->heap_break &&
       forked_mm->mmap_vmas.slots[0].end==TEST_USER_VA+4096,
       "COW fork shares read-only frame but owns a distinct MM layout");
    nas_mm_put(forked_mm);
    ck(reclaimed==1 && init->mm->users==1 && frame_owners==1,
       "forked MM last put releases only its owned address space");
    uint64_t tops[NAS_TASK_LIMIT];
    for (unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *t=nas_task_at(i);
        ck(t && nas_task_kernel_stack_ok(t),"each slot owns a valid stack");
        tops[i]=t->kernel_stack_top;
        ck((tops[i]&15U)==0,"16-byte stack ABI alignment");
        ck(tops[i]>=NAS_KERNEL_STACK_BYTES,"stack top underflow");
        for (unsigned j=0;j<i;j++) {
            uintptr_t a=(uintptr_t)tops[j]-NAS_KERNEL_STACK_BYTES;
            uintptr_t b=(uintptr_t)tops[i]-NAS_KERNEL_STACK_BYTES;
            ck(a+NAS_KERNEL_STACK_BYTES<=b ||
               b+NAS_KERNEL_STACK_BYTES<=a,"stack ranges disjoint");
        }
    }
    ck(!nas_task_kernel_stack_ok(NULL),"null rejected");
    /* A fork must not inherit a parent's live kernel continuation SP. */
    init->kernel_exec.cpu.saved_sp=tops[0]-128;
    struct nas_task fake={0};
    fake.kernel_stack_top=tops[0];
    ck(!nas_task_kernel_stack_ok(&fake),"foreign task rejected");
    for (unsigned i=1;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *child=nas_task_fork_slot();
        ck(child==nas_task_at(i),"fork slot identity");
        ck(child->kernel_stack_top==tops[i] &&
           child->kernel_stack_top!=init->kernel_stack_top,"no inherited stack");
        ck(nas_task_kernel_stack_ok(child),"fork stack intact");
        ck(child->kernel_exec.cpu.saved_sp==0 && !child->kernel_exec.wait.queue,
           "fork must not inherit parent kernel continuation");
        ck(child->mm==0,"new Task slot cannot inherit a borrowed MM pointer");
        ck(child->files && child->files!=init->files && child->fds==&child->files->table,
           "fork gives a new table owned by files object");
        ck(child->fs && child->fs!=init->fs && child->fs->umask==027U,
           "fork copies FS context, never a leader pointer");
        ck(child->sighand && child->sighand!=init->sighand &&
           child->sighand->actions[10][0]==123U,
           "fork copies live dispositions into independent resource");
        uint64_t *near_top=(uint64_t *)(uintptr_t)(tops[i]-64);
        *near_top=(uint64_t)(i*0x10203U); /* stack payload cannot alias */
    }
    ck(nas_task_fork_slot()==NULL,"full task capacity handled");
    for (unsigned i=1;i<NAS_TASK_LIMIT;i++) {
        uint64_t *near_top=(uint64_t *)(uintptr_t)(tops[i]-64);
        ck(*near_top==(uint64_t)(i*0x10203U),"stack payload isolated");
        nas_task_at(i)->kernel_exec.cpu.saved_sp=tops[i]-128;
        nas_task_release(nas_task_at(i));
        ck(nas_task_at(i)->kernel_exec.cpu.saved_sp==0,
           "recycled TID must not retain parked SP");
    }
    struct nas_task *reused=nas_task_fork_slot();
    ck(reused==nas_task_at(1) && reused->kernel_stack_top==tops[1] &&
       nas_task_kernel_stack_ok(reused),"reused slot recovers own stack");
    uint64_t *bottom=(uint64_t *)(uintptr_t)(tops[1]-NAS_KERNEL_STACK_BYTES);
    uint64_t saved=*bottom;
    *bottom=0;
    ck(!nas_task_kernel_stack_ok(reused),"stack sentinel overflow detected");
    *bottom=saved;
    ck(nas_task_kernel_stack_ok(reused),"restored sentinel accepted");
    /* A worker-created PROCESS is parented to TGID, not the caller's
     * transient TID. It remains wait4-owned after that worker exits. */
    struct nas_task *worker=reused;
    worker->is_thread=true;
    worker->tgid=init->tgid;
    worker->group_leader=init;
    worker->ppid=init->ppid;
    nas_current_task=worker;
    struct nas_task *grandchild=nas_task_fork_slot();
    ck(grandchild && grandchild->ppid==init->pid &&
       grandchild->pid!=worker->pid && !grandchild->is_thread,
       "worker fork process must have group leader PID as PPID");
    nas_current_task=init;
    grandchild->native_trap_frame=grandchild->kernel_stack_top-160U;
    grandchild->resume_valid=true;
    nas_task_release(grandchild);
    ck(!grandchild->native_trap_frame && !grandchild->resume_valid &&
       !grandchild->kernel_exec.cpu.saved_sp,
       "reclaimed task must have no stale kernel/user continuation");
    nas_task_release(worker);
    ck(init->pid==1 && init->mm->users==1,
       "worker/child reclamation preserves the init MM owner");
    printf("[TASK LIFETIME PASS] worker fork parent identity, detached slot cleanup\n");
    printf("[TASK STACK PASS] %u disjoint stacks, fork ownership, reuse, overflow sentinel\n", NAS_TASK_LIMIT);
    return 0;
}
