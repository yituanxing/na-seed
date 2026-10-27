#include <stdbool.h>
#include "sched_core.h"
#include "wait.h"
#include "../task.h"
static bool previous_kernel_slice;
void nas_sched_scan_init(struct nas_sched_scan *scan,unsigned start_slot) {
    if(!scan)return;
    scan->start_slot=start_slot%NAS_TASK_LIMIT;
    scan->offset=0;
}
struct nas_task *nas_sched_scan_next(struct nas_sched_scan *scan,
                                     unsigned *slot) {
    if(!scan)return 0;
    /* One FAIR ring scan. An awakened blocking syscall competes with
     * READY Tasks at its actual slot, not in a permanently deferred
     * second pass. The old READY-first scan starved Vim's UART select
     * whenever its housekeeping thread remained runnable; likewise
     * tmux/PTYS and blocked pipes under a busy producer.
     *
     * Readiness is a predicate on kernel-owned snapshots only. The
     * dispatcher switches CPU/MM into the selected Task BEFORE invoking
     * its saved kernel C continuation to complete the syscall. */
    while(scan->offset<NAS_TASK_LIMIT) {
        unsigned candidate_slot=(scan->start_slot+scan->offset++)%NAS_TASK_LIMIT;
        struct nas_task *candidate=nas_task_at(candidate_slot);
        if(!nas_sched_user_eligible(candidate))
            continue;
        if(slot)*slot=candidate_slot;
        return candidate;
    }
    return 0;
}
bool nas_sched_user_eligible(const struct nas_task *candidate) {
    if(!candidate || !candidate->resume_valid)return false;
    return candidate->state==NAS_TASK_READY ||
           (candidate->state==NAS_TASK_BLOCKED &&
            nas_task_wait_ready(candidate));
}
bool nas_sched_has_ready_users(void) {
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        const struct nas_task *t=nas_task_at(slot);
        if(nas_sched_user_eligible(t))return true;
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
