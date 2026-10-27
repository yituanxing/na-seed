#include <stdint.h>
#include "../task.h"
#include "../errno.h"
#include "session.h"
/* Linux pid_t is signed 32-bit on all three LP64 target architectures.
 * This is the shared semantic layer AFTER the architecture syscall decoder.
 */
static int64_t pid32(uint64_t raw) {
    return (int64_t)(int32_t)(uint32_t)raw;
}
static struct nas_task *target(uint64_t raw) {
    int64_t pid=pid32(raw);
    if (pid<0) return 0;
    struct nas_task *t=pid==0 ? nas_current_task->group_leader :
                                     nas_task_by_pid((uint64_t)pid);
    return t && !t->is_thread ? t : 0;
}
uint64_t nas_session_getpgid(uint64_t raw_pid) {
    struct nas_task *task=target(raw_pid);
    return task ? task->pgid : (uint64_t)-ESRCH;
}
uint64_t nas_session_getsid(uint64_t raw_pid) {
    struct nas_task *task=target(raw_pid);
    return task ? task->sid : (uint64_t)-ESRCH;
}
/* PGID and SID describe a process; every live sibling sees one identity. */
static void publish_groups(uint64_t tgid,uint64_t pgid,uint64_t sid) {
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *member=nas_task_at(i);
        if(member && member->state!=NAS_TASK_FREE && member->tgid==tgid) {
            member->pgid=pgid;
            member->sid=sid;
        }
    }
}
uint64_t nas_session_setsid(void) {
    struct nas_task *self=nas_current_task;
    uint64_t pid=nas_task_process_pid(self);
    if(pid==self->pgid)return (uint64_t)-EPERM;
    publish_groups(pid,pid,pid);
    return pid;
}
uint64_t nas_session_setpgid(uint64_t raw_pid,uint64_t raw_pgid) {
    int64_t pid=pid32(raw_pid), group=pid32(raw_pgid);
    if (pid<0 || group<0) return (uint64_t)-EINVAL;
    struct nas_task *self=nas_current_task;
    struct nas_task *task=target(raw_pid);
    if (!task || (task->tgid!=self->tgid &&
                  task->ppid!=nas_task_process_pid(self)))
        return (uint64_t)-ESRCH;
    if (task->sid!=self->sid) return (uint64_t)-EPERM;
    if (task->tgid!=self->tgid && task->execed) return (uint64_t)-EACCES;
    if (task->sid==nas_task_process_pid(task)) return (uint64_t)-EPERM;
    uint64_t new_group=group ? (uint64_t)group : nas_task_process_pid(task);
    if (new_group!=nas_task_process_pid(task)) {
        /* Existing group in the SAME session is mandatory. A group
         * can't be invented with the PID of a different process. */
        bool exists=false;
        for (unsigned i=0;i<NAS_TASK_LIMIT;++i) {
            struct nas_task *member=nas_task_at(i);
            if (member && member->state!=NAS_TASK_FREE &&
                member->pgid==new_group && member->sid==self->sid) {
                exists=true;
                break;
            }
        }
        if (!exists) return (uint64_t)-EPERM;
    }
    publish_groups(task->tgid,new_group,task->sid);
    return 0;
}
