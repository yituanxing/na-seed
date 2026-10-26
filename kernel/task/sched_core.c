#include <stdbool.h>
#include "sched_core.h"
#include "wait.h"
#include "../task.h"
static bool previous_kernel_slice;
void nas_sched_scan_init(struct nas_sched_scan *scan,
                         const struct nas_task *exclude,
                         unsigned start_slot,unsigned visit_limit) {
    if(!scan)return;
    scan->exclude=exclude;
    scan->start_slot=start_slot%NAS_TASK_LIMIT;
    scan->visit_limit=visit_limit>NAS_TASK_LIMIT ?
                       NAS_TASK_LIMIT : visit_limit;
    scan->offset=0;
    scan->pass=0;
}
struct nas_task *nas_sched_scan_next(struct nas_sched_scan *scan,
                                     unsigned *slot) {
    if(!scan)return 0;
    while(scan->pass<2) {
        while(scan->offset<scan->visit_limit) {
            unsigned candidate_slot=(scan->start_slot+scan->offset++)%NAS_TASK_LIMIT;
            struct nas_task *candidate=nas_task_at(candidate_slot);
            if(!nas_sched_user_eligible(candidate,scan->exclude,scan->pass))
                continue;
            if(slot)*slot=candidate_slot;
            return candidate;
        }
        ++scan->pass;
        scan->offset=0;
    }
    return 0;
}
bool nas_sched_user_commit(struct nas_task *previous,
                           struct nas_task *next,bool preempt_previous) {
    if(!next || !next->resume_valid ||
       next->state==NAS_TASK_FREE || next->state==NAS_TASK_ZOMBIE ||
       next->state==NAS_TASK_STOPPED ||
       (preempt_previous && (!previous || previous==next ||
                             previous->state!=NAS_TASK_RUNNING)))
        return false;
    if(preempt_previous) {
        previous->state=NAS_TASK_READY;
        previous->resume_valid=true;
    }
    next->state=NAS_TASK_RUNNING;
    next->resume_valid=false;
    return true;
}

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
