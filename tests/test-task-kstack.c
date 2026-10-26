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
uint64_t nas_mmu_fork_root(uint64_t source_root) {
    seen_source_root=source_root;
    return source_root==0x2000 ? fork_root : 0;
}
bool nas_mmu_resolve_user_address(uint64_t source_root,uint64_t va,
                                  uint64_t *physical,unsigned *access) {
    if(source_root!=0x2000 || va!=0x4008 || !physical)return false;
    *physical=0x9008;
    if(access)*access=1;
    return true;
}
bool nas_mmu_walk_user_mapping(uint64_t source_root,
                               bool (*visit)(uint64_t,unsigned,void *),void *ctx) {
    return source_root==0x2000 && visit(0x4000,1,ctx);
}
static bool see_page(uint64_t va,unsigned prot,void *opaque) {
    uint64_t *count=opaque;
    if(va!=0x4000 || prot!=1)return false;
    ++*count;
    return true;
}
bool nas_mmu_reclaim_root(uint64_t root) {
    if (root!=fork_root) abort();
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
    ck(nas_mm_resolve_user(init->mm,0x4008,&pa,&prot) &&
       pa==0x9008 && prot==1,"MM explicit lookup selects its own root");
    ck(nas_mm_walk_pages(init->mm,see_page,&count) && count==1,
       "MM page walk is independent of Task leader");
    ck(!nas_mm_resolve_user(init->mm,0x5000,&pa,&prot),
       "MM cannot claim an absent PTE");
    init->mm->heap_break=0x12345000ULL;
    init->mm->mmap_vmas.slots[0].start=0x20000000ULL;
    init->mm->mmap_vmas.slots[0].end=0x20001000ULL;
    struct nas_mm *forked_mm=nas_mm_fork_eager(init->mm);
    ck(forked_mm && forked_mm!=init->mm &&
       seen_source_root==init->mm->root_phys &&
       forked_mm->root_phys==fork_root &&
       forked_mm->heap_break==init->mm->heap_break &&
       forked_mm->mmap_vmas.slots[0].end==0x20001000ULL,
       "eager fork owns a distinct MM and copied layout");
    nas_mm_put(forked_mm);
    ck(reclaimed==1 && init->mm->users==1,
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
    init->kernel_context.saved_sp=tops[0]-128;
    struct nas_task fake={0};
    fake.kernel_stack_top=tops[0];
    ck(!nas_task_kernel_stack_ok(&fake),"foreign task rejected");
    for (unsigned i=1;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *child=nas_task_fork_slot();
        ck(child==nas_task_at(i),"fork slot identity");
        ck(child->kernel_stack_top==tops[i] &&
           child->kernel_stack_top!=init->kernel_stack_top,"no inherited stack");
        ck(nas_task_kernel_stack_ok(child),"fork stack intact");
        ck(child->kernel_context.saved_sp==0,
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
        nas_task_at(i)->kernel_context.saved_sp=tops[i]-128;
        nas_task_release(nas_task_at(i));
        ck(nas_task_at(i)->kernel_context.saved_sp==0,
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
    printf("[TASK STACK PASS] %u disjoint stacks, fork ownership, reuse, overflow sentinel\n", NAS_TASK_LIMIT);
    return 0;
}
