#include "../task.h"
#include "run_state.h"
/* Futex/event producers only publish READY state: the dispatcher resumes
 * the actual saved kernel C continuation in this Task's MM. */
bool nas_task_wake_ready(struct nas_task *task) {
    if(!task || task->state!=NAS_TASK_BLOCKED ||
       !task->resume_valid || !task->wait.pending_valid)
        return false;
    task->state=NAS_TASK_READY;
    return true;
}
