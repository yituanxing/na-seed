#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/futex_wait.h"
#include "../kernel/syscall/ops.h"
static struct nas_mm mm1,mm2;
static struct nas_task a,b,c,d,e;
static void arm(struct nas_task *t,struct nas_mm *mm,uint64_t va,uint32_t mask) {
    *t=(struct nas_task){0};
    t->mm=mm;t->wait.futex_addr=va;t->wait.futex_mask=mask;
    assert(nas_futex_wait_arm(t));
    t->wait.pending_valid=true;t->wait.blocked_op=NAS_SC_futex;
    t->state=NAS_TASK_BLOCKED;
    t->resume_valid=true;
}
int main(void) {
    arm(&a,&mm1,0x1000,1);arm(&b,&mm1,0x1000,2);
    arm(&c,&mm1,0x1000,3);arm(&d,&mm2,0x1000,1);
    arm(&e,&mm1,0x2000,1);
    assert(!nas_futex_wait_arm(&a));
    assert(nas_futex_wait_wake(&mm1,0x1000,1,2)==1);
    assert(b.wait.futex_woken && b.state==NAS_TASK_READY &&
           a.state==NAS_TASK_BLOCKED && !c.wait.futex_woken);
    assert(!b.kernel_wait.queue);
    assert(nas_futex_wait_wake(&mm1,0x1000,0,~0U)==0);
    assert(nas_futex_wait_requeue(&mm1,0x1000,0x2000,1)==1);
    assert(a.wait.futex_addr==0x2000);
    assert(nas_futex_wait_wake(&mm1,0x1000,4,1)==1);
    assert(c.wait.futex_woken && c.state==NAS_TASK_READY &&
           !d.wait.futex_woken && d.state==NAS_TASK_BLOCKED);
    assert(nas_futex_wait_wake(&mm1,0x2000,4,1)==2);
    assert(a.wait.futex_woken && e.wait.futex_woken &&
           a.state==NAS_TASK_READY && e.state==NAS_TASK_READY);
    assert(nas_futex_wait_wake(&mm2,0x1000,4,1)==1);
    assert(d.wait.futex_woken);
    arm(&a,&mm1,0x4000,1);arm(&c,&mm1,0x4000,1);
    assert(nas_futex_wait_requeue(&mm1,0x4000,0x4000,2)==0);
    assert(nas_futex_wait_requeue(&mm1,0x4000,0x4040,2)==2);
    assert(a.wait.futex_addr==0x4040 && c.wait.futex_addr==0x4040);
    nas_futex_wait_cancel(&a);
    assert(!a.kernel_wait.queue);
    assert(nas_futex_wait_wake(&mm1,0x4040,2,1)==1);
    assert(c.wait.futex_woken && !a.wait.futex_woken);
    puts("[M93 FUTEX PASS] mm-keyed, masked wake, requeue and cancellation");
    return 0;
}
