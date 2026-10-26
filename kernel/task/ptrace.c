#include <stdint.h>
#include <stdbool.h>
#include "ptrace.h"
#include "../task.h"
#include "../errno.h"
#include "signal.h"
#include "sched.h"
#include "../arch/arch.h"
#include "../arch/ptrace_regs.h"
#include "../mm/usercopy.h"
#include "../mm/vm.h"
#include "../lib/string.h"

#define PTRACE_TRACEME 0ULL
#define PTRACE_PEEKTEXT 1ULL
#define PTRACE_PEEKDATA 2ULL
#define PTRACE_CONT 7ULL
#define PTRACE_GETREGS 12ULL
#define PTRACE_DETACH 17ULL
#define PTRACE_SYSCALL 24ULL
#define PTRACE_SETOPTIONS 0x4200ULL
#define PTRACE_SEIZE 0x4206ULL
#define PTRACE_INTERRUPT 0x4207ULL
#define PTRACE_GETREGSET 0x4204ULL
#define PTRACE_GET_SYSCALL_INFO 0x420eULL
#define PTRACE_O_TRACESYSGOOD 1ULL
#define NT_PRSTATUS 1ULL
#define SYSINFO_ENTRY 1U
#define SYSINFO_EXIT 2U
static bool ptrace_owner_valid(const struct nas_task *task) {
    return task && task->ptrace_tracer_pid &&
        nas_task_by_pid(task->ptrace_tracer_pid)!=0;
}
static void ptrace_stop(struct nas_task *task,uint8_t phase) {
    task->ptrace_phase=phase;
    task->resume_valid=true;
    task->stopped_from=NAS_TASK_READY;
    task->state=NAS_TASK_STOPPED;
    task->stop_signal=NAS_SIGTRAP |
        ((task->ptrace_options&PTRACE_O_TRACESYSGOOD)?0x80U:0U);
    task->stop_report_pending=true;
    task->continue_report_pending=false;
}
bool nas_ptrace_syscall_enter(struct nas_task *task,const uint64_t *call) {
    if(!task)return false;
    if(task->ptrace_skip_entry_once) {
        task->ptrace_skip_entry_once=false;
        return false;
    }
    if(!task->ptrace_trace_syscalls || !ptrace_owner_valid(task))
        return false;
    nas_arch_save_user_frame(task,call);
    nas_arch_save_fpu(task);
    /* User instruction has already advanced in all native gates. Rewind
     * only the SAVED tracee context; actual syscall runs exactly once
     * when tracer resumes this stopped Task. */
    nas_arch_ptrace_rewind_syscall(&task->arch);
    for(unsigned i=0;i<7;i++)task->ptrace_call[i]=call[i];
    task->ptrace_ip=call[7] -
#if defined(__x86_64__)
        2ULL;
#else
        4ULL;
#endif
    task->ptrace_sp=nas_arch_signal_user_sp();
    ptrace_stop(task,SYSINFO_ENTRY);
    return true;
}
bool nas_ptrace_syscall_exit(struct nas_task *task,const uint64_t *call,
                              uint64_t value) {
    if(!task || !task->ptrace_trace_syscalls || !ptrace_owner_valid(task))
        return false;
    nas_arch_save_user_frame(task,call);
    nas_arch_save_fpu(task);
    nas_arch_set_saved_return(task,value);
    task->ptrace_result=value;
    task->ptrace_ip=call[7];
    task->ptrace_sp=nas_arch_signal_user_sp();
    ptrace_stop(task,SYSINFO_EXIT);
    return true;
}
static uint64_t ptrace_syscall_info(const struct nas_task *task,
                                    uint64_t len,uint64_t dest) {
    struct nas_sysinfo {
        uint8_t op,pad[3];
        uint32_t arch;
        uint64_t ip,sp;
        union {
            struct {uint64_t nr,args[6];} entry;
            struct {int64_t rval;uint8_t is_error;uint8_t pad[7];} exit;
        } details;
    } info={0};
    _Static_assert(sizeof(info)==80,"Linux ptrace_syscall_info prefix");
#if defined(__x86_64__)
    info.arch=0xc000003eU;
#elif defined(__riscv) && __riscv_xlen == 64
    info.arch=0xc00000f3U;
#else
    info.arch=0xc00000b7U;
#endif
    info.ip=task->ptrace_ip;
    info.sp=task->ptrace_sp;
    if((task->ptrace_options & PTRACE_O_TRACESYSGOOD) &&
       task->ptrace_phase==SYSINFO_ENTRY) {
        info.op=SYSINFO_ENTRY;
        info.details.entry.nr=task->ptrace_call[0];
        for(unsigned i=0;i<6;i++)info.details.entry.args[i]=task->ptrace_call[i+1];
    } else if((task->ptrace_options & PTRACE_O_TRACESYSGOOD) &&
              task->ptrace_phase==SYSINFO_EXIT) {
        info.op=SYSINFO_EXIT;
        info.details.exit.rval=(int64_t)task->ptrace_result;
        info.details.exit.is_error=(int64_t)task->ptrace_result<0 &&
                                      (int64_t)task->ptrace_result>=-4095;
    }
    /* Kernel returns available bytes, even when caller has a short buffer.
     * Entry needs 24+56, exit 24+9, NONE 24 bytes. */
    uint64_t available=info.op==SYSINFO_ENTRY?80:
                       info.op==SYSINFO_EXIT?33:24;
    if(len>available)len=available;
    if(len && !nas_user_write_bytes(dest,(const uint8_t *)&info,len))
        return (uint64_t)-EFAULT;
    return available;
}
static uint64_t ptrace_gprs(const struct nas_task *task,
                           uint64_t request,uint64_t addr,uint64_t data) {
    uint64_t words[34]={0};
    unsigned n=nas_arch_ptrace_gpr_words(task,words);
    uint64_t len=n*8ULL;
    if(request==PTRACE_GETREGSET) {
        if(addr!=NT_PRSTATUS)return (uint64_t)-EINVAL;
        uint64_t iov[2];
        if(!nas_user_read_bytes(data,(uint8_t *)iov,sizeof(iov)))
            return (uint64_t)-EFAULT;
        if(iov[1]<len)len=iov[1];
        if(len && !nas_user_write_bytes(iov[0],(const uint8_t *)words,len))
            return (uint64_t)-EFAULT;
        return nas_user_write_u64(data+8,len)?0:(uint64_t)-EFAULT;
    }
#if defined(__x86_64__)
    if(request==PTRACE_GETREGS)
        return nas_user_write_bytes(data,(const uint8_t *)words,len)
            ?0:(uint64_t)-EFAULT;
#endif
    return (uint64_t)-EOPNOTSUPP;
}
uint64_t nas_ptrace_request(uint64_t request,uint64_t raw_pid,
                             uint64_t addr,uint64_t data) {
    struct nas_task *self=nas_current_task;
    /* Unlike a supported operation on a tracee we do not own, these
     * commands are entirely unimplemented. Linux before PTRACE_SEIZE
     * existed returned EIO: crucially NOT EPERM (which strace uses as
     * evidence of modern SEIZE support when probing ptrace(self)).
     * Correct unsupported-operation semantics select legacy TRACEME. */
    if(request==PTRACE_SEIZE || request==PTRACE_INTERRUPT)
        return (uint64_t)-EIO;
    if(request==PTRACE_TRACEME) {
        if(!self || self->pid==1 || self->ptrace_tracer_pid || !self->ppid)
            return (uint64_t)-EPERM;
        struct nas_task *parent=nas_task_by_pid(self->ppid);
        if(!parent || parent==self || parent->state==NAS_TASK_FREE ||
           parent->state==NAS_TASK_ZOMBIE)
            return (uint64_t)-EPERM;
        self->ptrace_tracer_pid=parent->pid;
        self->ptrace_options=0;
        self->ptrace_phase=0;
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
        case PTRACE_SETOPTIONS:
            /* No fake TRACEFORK/TRACEEXEC/EXITKILL: those events must
             * actually be produced before advertising their options. */
            if(data & ~PTRACE_O_TRACESYSGOOD)return (uint64_t)-EINVAL;
            target->ptrace_options=data;
            return 0;
        case PTRACE_GET_SYSCALL_INFO:
            return ptrace_syscall_info(target,addr,data);
        case PTRACE_GETREGSET:
        case PTRACE_GETREGS:
            return ptrace_gprs(target,request,addr,data);
        case PTRACE_PEEKDATA:
        case PTRACE_PEEKTEXT: {
            uint64_t word=0;
            int64_t count=nas_vm_read_task_bytes(target,addr,
                                                  (uint8_t *)&word,sizeof(word));
            if(count<0)return (uint64_t)count;
            return count==(int64_t)sizeof(word)?word:(uint64_t)-EFAULT;
        }
        case PTRACE_CONT:
        case PTRACE_SYSCALL:
        case PTRACE_DETACH:
            if(data)return (uint64_t)-EOPNOTSUPP; /* signal injection */
            target->ptrace_trace_syscalls=request==PTRACE_SYSCALL;
            if(target->ptrace_phase==SYSINFO_ENTRY)
                target->ptrace_skip_entry_once=true;
            target->ptrace_phase=0;
            target->state=target->stopped_from==NAS_TASK_BLOCKED
                ? NAS_TASK_BLOCKED : NAS_TASK_READY;
            target->stopped_from=NAS_TASK_FREE;
            target->stop_report_pending=false;
            target->continue_report_pending=false;
            if(request==PTRACE_DETACH) {
                target->ptrace_tracer_pid=0;
                target->ptrace_options=0;
            }
            return 0;
        default:
            return (uint64_t)-EOPNOTSUPP;
    }
}
