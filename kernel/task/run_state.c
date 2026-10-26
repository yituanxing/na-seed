#include "../task.h"
#include "run_state.h"
/* A parked C continuation and the historical saved-user-frame fallback
 * share the same READY publication. No ready-to-run stack is synthesized. */
bool nas_task_wake_ready(struct nas_task *task) {
    if(!task || task->state!=NAS_TASK_BLOCKED ||
       !task->resume_valid || !task->wait.pending_valid)
        return false;
    task->state=NAS_TASK_READY;
    return true;
}
