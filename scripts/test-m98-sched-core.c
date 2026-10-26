#include <assert.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/sched_core.h"
static struct nas_task table[NAS_TASK_LIMIT];
struct nas_task *nas_task_at(unsigned slot) {
    return slot<NAS_TASK_LIMIT?&table[slot]:0;
}
bool nas_task_wait_ready(const struct nas_task *task) {
    return task && task->state==NAS_TASK_BLOCKED &&
           task->resume_valid && task->wait.futex_woken;
}
int main(void) {
    struct nas_task *running=&table[0],*ready=&table[1],*blocked=&table[2];
    running->state=NAS_TASK_RUNNING;running->resume_valid=true;
    ready->state=NAS_TASK_READY;ready->resume_valid=true;
    blocked->state=NAS_TASK_BLOCKED;blocked->resume_valid=true;
    assert(!nas_sched_user_eligible(running,running,0));
    assert(nas_sched_user_eligible(ready,running,0));
    assert(!nas_sched_user_eligible(blocked,running,0));
    assert(!nas_sched_user_eligible(blocked,running,1));
    blocked->wait.futex_woken=true;
    assert(nas_sched_user_eligible(blocked,running,1));
    assert(!nas_sched_user_eligible(ready,running,1));
    assert(!nas_sched_user_eligible(ready,running,2));
    assert(nas_sched_has_ready_users());
    assert(nas_sched_allow_kernel_slice(true,true));
    assert(!nas_sched_allow_kernel_slice(true,true));
    assert(nas_sched_allow_kernel_slice(true,true));
    assert(!nas_sched_allow_kernel_slice(true,false));
    assert(nas_sched_allow_kernel_slice(true,true));
    assert(nas_sched_allow_kernel_slice(false,true));
    assert(!nas_sched_allow_kernel_slice(true,true));
    assert(!nas_sched_allow_kernel_slice(false,false));
    assert(nas_sched_allow_kernel_slice(true,true));
    ready->state=NAS_TASK_STOPPED;
    assert(!nas_sched_has_ready_users());
    ready->state=NAS_TASK_ZOMBIE;
    assert(!nas_sched_has_ready_users());
    running->state=NAS_TASK_READY; /* can be re-enqueued after preemption */
    assert(nas_sched_has_ready_users());
    puts("[M98 SCHED PASS] shared IRQ/syscall candidate rules, kernel/user RR fairness, stopped exclusion");
}
