#include <assert.h>
#include <stdio.h>
#include "../kernel/task.h"
#include "../kernel/task/run_state.h"
int main(void) {
    struct nas_task t={0};
    assert(!nas_task_wake_ready(0));
    assert(!nas_task_wake_ready(&t));
    t.state=NAS_TASK_BLOCKED;
    assert(!nas_task_wake_ready(&t));
    t.wait.pending_valid=true;
    assert(!nas_task_wake_ready(&t));
    t.resume_valid=true;
    assert(nas_task_wake_ready(&t));
    assert(t.state==NAS_TASK_READY && t.resume_valid &&
           t.wait.pending_valid); /* actual syscall completion still pending */
    assert(!nas_task_wake_ready(&t));
    t.state=NAS_TASK_STOPPED;
    assert(!nas_task_wake_ready(&t));
    t.state=NAS_TASK_ZOMBIE;
    assert(!nas_task_wake_ready(&t));
    puts("[M94 RUNSTATE PASS] one producer wake, no double wake, no STOPPED/zombie transition");
}
