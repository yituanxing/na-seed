/* M27: host-side ownership / fork / reuse tests for actual kernel/task.c.
 * The real stock BusyBox regression separately tests live QEMU transitions. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/task.h"

/* Exercise the real MM ownership implementation. Only hardware page-table
 * primitives and FD bootstrap are replaced in this CPU-independent test. */
void nas_fd_system_init(void) {}
void nas_print(const char *msg) { fputs(msg, stderr); }
__attribute__((noreturn)) void nas_arch_halt(void) { abort(); }
static uint64_t fork_root=0x3000;
static unsigned reclaimed;
uint64_t nas_mmu_fork_root(void) { return fork_root; }
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
    nas_mm_get(init->mm);
    ck(init->mm->users==2,"MM get adds one owner");
    nas_mm_put(init->mm);
    ck(init->mm->users==1,"MM put preserves active owner");
    init->mm->heap_break=0x12345000ULL;
    init->mm->anonymous.slots[0].start=0x20000000ULL;
    init->mm->anonymous.slots[0].end=0x20001000ULL;
    struct nas_mm *forked_mm=nas_mm_fork_eager(init->mm);
    ck(forked_mm && forked_mm!=init->mm &&
       forked_mm->root_phys==fork_root &&
       forked_mm->heap_break==init->mm->heap_break &&
       forked_mm->anonymous.slots[0].end==0x20001000ULL,
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
    struct nas_task fake={0};
    fake.kernel_stack_top=tops[0];
    ck(!nas_task_kernel_stack_ok(&fake),"foreign task rejected");
    for (unsigned i=1;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *child=nas_task_fork_slot();
        ck(child==nas_task_at(i),"fork slot identity");
        ck(child->kernel_stack_top==tops[i] &&
           child->kernel_stack_top!=init->kernel_stack_top,"no inherited stack");
        ck(nas_task_kernel_stack_ok(child),"fork stack intact");
        ck(child->mm==0,"new Task slot cannot inherit a borrowed MM pointer");
        uint64_t *near_top=(uint64_t *)(uintptr_t)(tops[i]-64);
        *near_top=(uint64_t)(i*0x10203U); /* stack payload cannot alias */
    }
    ck(nas_task_fork_slot()==NULL,"full task capacity handled");
    for (unsigned i=1;i<NAS_TASK_LIMIT;i++) {
        uint64_t *near_top=(uint64_t *)(uintptr_t)(tops[i]-64);
        ck(*near_top==(uint64_t)(i*0x10203U),"stack payload isolated");
        nas_task_release(nas_task_at(i));
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
