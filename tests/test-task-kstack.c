/* M27: host-side ownership / fork / reuse tests for actual kernel/task.c.
 * The real stock BusyBox regression separately tests live QEMU transitions. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/task.h"

/* Only the FD bootstrap is irrelevant to static Task slot ownership. */
void nas_fd_system_init(void) {}
static void ck(int condition, const char *why) {
    if (!condition) { fprintf(stderr,"[TASK STACK FAIL] %s\n",why); exit(1); }
}
int main(void) {
    nas_task_boot_init();
    struct nas_task *init=nas_task_current();
    ck(init==nas_task_at(0) && init->pid==1,"PID1 owner");
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
