#include <stdint.h>
#include "ptrace.h"
#include "../task.h"
#include "../errno.h"

#define NAS_PTRACE_TRACEME 0ULL
#define NAS_PTRACE_CONT 7ULL
#define NAS_PTRACE_DETACH 17ULL

/* Task identity/relationship and signal stop ownership are generic.
 * No lookup or dereference through a foreign userspace MMU; the argument
 * semantics for architecture register read/write are NOT supported here. */
uint64_t nas_ptrace_request(uint64_t request,uint64_t raw_pid,
                             uint64_t addr,uint64_t data) {
    struct nas_task *self=nas_current_task;
    (void)addr;
    if(request==NAS_PTRACE_TRACEME) {
        if(!self || self->pid==1 || self->ptrace_tracer_pid || !self->ppid)
            return (uint64_t)-EPERM;
        struct nas_task *parent=nas_task_by_pid(self->ppid);
        if(!parent || parent==self || parent->state==NAS_TASK_FREE ||
           parent->state==NAS_TASK_ZOMBIE)
            return (uint64_t)-EPERM;
        self->ptrace_tracer_pid=parent->pid;
        return 0;
    }
    if(!self || raw_pid>UINT32_MAX || !raw_pid)return (uint64_t)-ESRCH;
    struct nas_task *target=nas_task_by_pid(raw_pid);
    if(!target || target->state==NAS_TASK_FREE ||
       target->state==NAS_TASK_ZOMBIE)return (uint64_t)-ESRCH;
    if(target->ptrace_tracer_pid!=self->pid)return (uint64_t)-EPERM;
    if(target->state!=NAS_TASK_STOPPED ||
       target->stopped_from==NAS_TASK_FREE)return (uint64_t)-ESRCH;
    switch(request) {
        case NAS_PTRACE_CONT:
        case NAS_PTRACE_DETACH:
            if(data)return (uint64_t)-EOPNOTSUPP; /* signal injection */
            target->state=target->stopped_from==NAS_TASK_BLOCKED
                ? NAS_TASK_BLOCKED : NAS_TASK_READY;
            target->stopped_from=NAS_TASK_FREE;
            target->stop_report_pending=false;
            target->continue_report_pending=false;
            if(request==NAS_PTRACE_DETACH)target->ptrace_tracer_pid=0;
            return 0;
        default:
            /* GETREGSET, PEEKDATA, SYSCALL, GET_SYSCALL_INFO etc. MUST
             * not claim success without correct live architecture data. */
            return (uint64_t)-EOPNOTSUPP;
    }
}
