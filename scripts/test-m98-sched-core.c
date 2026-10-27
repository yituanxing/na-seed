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
    assert(!nas_sched_user_eligible(running,running));
    assert(nas_sched_user_eligible(ready,running));
    assert(!nas_sched_user_eligible(blocked,running));
    blocked->wait.futex_woken=true;
    assert(nas_sched_user_eligible(blocked,running));
    assert(nas_sched_user_eligible(ready,running));
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
    /* A woken I/O waiter counts as runnable user work, even without a
     * task already marked READY. Kernel workers must not steal its slice. */
    assert(nas_sched_has_ready_users());
    blocked->wait.futex_woken=false;
    assert(!nas_sched_has_ready_users());
    ready->state=NAS_TASK_ZOMBIE;
    assert(!nas_sched_has_ready_users());
    running->state=NAS_TASK_READY; /* can be re-enqueued after preemption */
    assert(nas_sched_has_ready_users());
    /* M99: IRQ and syscall use the same rotating eligibility iterator;
     * a woken BLOCKED Task must not starve behind a READY worker. */
    struct nas_sched_scan scan;
    unsigned slot=NAS_TASK_LIMIT;
    running->state=NAS_TASK_RUNNING;running->resume_valid=true;
    ready->state=NAS_TASK_READY;ready->resume_valid=true;
    blocked->state=NAS_TASK_BLOCKED;blocked->resume_valid=true;
    blocked->wait.futex_woken=true;
    nas_sched_scan_init(&scan,running,1,NAS_TASK_LIMIT-1);
    assert(nas_sched_scan_next(&scan,&slot)==ready && slot==1);
    assert(nas_sched_scan_next(&scan,&slot)==blocked && slot==2);
    assert(!nas_sched_scan_next(&scan,&slot));
    /* Previously READY-first pass returned slot1 ahead of slot2
     * even though the cursor stood exactly at awakened slot2. */
    nas_sched_scan_init(&scan,running,2,NAS_TASK_LIMIT);
    assert(nas_sched_scan_next(&scan,&slot)==blocked && slot==2);
    assert(nas_sched_scan_next(&scan,&slot)==ready && slot==1);
    assert(!nas_sched_scan_next(&scan,&slot));
    assert(nas_sched_user_commit(running,ready,true));
    assert(running->state==NAS_TASK_READY && running->resume_valid);
    assert(ready->state==NAS_TASK_RUNNING && !ready->resume_valid);
    assert(!nas_sched_user_commit(running,blocked,true));
    /* Cooperative scheduling never changes a sleeping/retired previous. */
    running->state=NAS_TASK_BLOCKED;
    blocked->state=NAS_TASK_BLOCKED;
    blocked->resume_valid=true;
    assert(nas_sched_user_commit(running,blocked,false));
    assert(running->state==NAS_TASK_BLOCKED &&
           blocked->state==NAS_TASK_RUNNING && !blocked->resume_valid);
    blocked->state=NAS_TASK_ZOMBIE;blocked->resume_valid=true;
    assert(!nas_sched_user_commit(ready,blocked,false));
    ready->state=NAS_TASK_READY;ready->resume_valid=true;
    nas_sched_scan_init(&scan,0,NAS_TASK_LIMIT-1,NAS_TASK_LIMIT);
    assert(nas_sched_scan_next(&scan,&slot)==ready && slot==1);
    assert(!nas_sched_scan_next(&scan,&slot));
    /* A STOPPED task stays excluded even if it carries a saved frame. */
    ready->state=NAS_TASK_STOPPED;
    nas_sched_scan_init(&scan,0,0,NAS_TASK_LIMIT);
    assert(!nas_sched_scan_next(&scan,&slot));
    puts("[M99 SCHED PASS] common IRQ/syscall circular fairness, I/O wake no-starvation, commit and exclusion");
    puts("[M98 SCHED PASS] shared IRQ/syscall candidate rules, kernel/user RR fairness, stopped exclusion");
}
