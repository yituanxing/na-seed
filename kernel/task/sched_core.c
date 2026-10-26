#include <stdbool.h>
#include "sched_core.h"
#include "wait.h"
#include "../task.h"
static bool previous_kernel_slice;
bool nas_sched_user_eligible(const struct nas_task *candidate,
                             const struct nas_task *running,unsigned pass) {
    if(!candidate || !candidate->resume_valid ||
       (candidate==running && candidate->state==NAS_TASK_RUNNING))
        return false;
    if(pass==0)return candidate->state==NAS_TASK_READY;
    if(pass==1)return nas_task_wait_ready(candidate);
    return false;
}
bool nas_sched_has_ready_users(void) {
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        const struct nas_task *t=nas_task_at(slot);
        if(t && t->resume_valid && t->state==NAS_TASK_READY)return true;
    }
    return false;
}
bool nas_sched_allow_kernel_slice(bool user_ready,bool kernel_ready) {
    if(!kernel_ready) {
        previous_kernel_slice=false;
        return false;
    }
    if(!user_ready) {
        previous_kernel_slice=true;
        return true;
    }
    /* Alternate when both domains have ready work. Never permit a yielding
     * kernel worker to take every user scheduling boundary. */
    previous_kernel_slice=!previous_kernel_slice;
    return previous_kernel_slice;
}
