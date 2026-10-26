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
#define PTRACE_ATTACH 16ULL
#define PTRACE_PEEKTEXT 1ULL
#define PTRACE_PEEKDATA 2ULL
#define PTRACE_POKETEXT 4ULL
#define PTRACE_POKEDATA 5ULL
#define PTRACE_SETREGS 13ULL
#define PTRACE_CONT 7ULL
#define PTRACE_GETREGS 12ULL
#define PTRACE_DETACH 17ULL
#define PTRACE_SYSCALL 24ULL
#define PTRACE_SETOPTIONS 0x4200ULL
#define PTRACE_GETEVENTMSG 0x4201ULL
#define PTRACE_SEIZE 0x4206ULL
#define PTRACE_INTERRUPT 0x4207ULL
#define PTRACE_GETREGSET 0x4204ULL
#define PTRACE_SETREGSET 0x4205ULL
#define PTRACE_GET_SYSCALL_INFO 0x420eULL
#define PTRACE_O_TRACESYSGOOD 1ULL
#define PTRACE_O_TRACEFORK 2ULL
#define PTRACE_O_TRACEVFORK 4ULL
#define PTRACE_O_TRACECLONE 8ULL
#define PTRACE_O_TRACESECCOMP 0x80ULL
#define PTRACE_O_EXITKILL 0x100000ULL
#define PTRACE_EVENT_FORK 1U
#define PTRACE_EVENT_VFORK 2U
#define PTRACE_EVENT_CLONE 3U
#define PTRACE_O_TRACEEXEC 0x10ULL
#define PTRACE_O_TRACEEXIT 0x40ULL
#define PTRACE_EVENT_EXEC 4U
#define PTRACE_EVENT_EXIT 6U
#define NT_PRSTATUS 1ULL
#define SYSINFO_ENTRY 1U
#define SYSINFO_EXIT 2U
/* EXITKILL policy belongs to ptrace/task lifetime, not generic Task slot
 * administration. Reuse the real signal/group-exit mechanism; Task core
 * therefore does not gain a hidden dependency on signal service. */
void nas_ptrace_tracer_exit(uint64_t tracer_pid) {
    if(!tracer_pid)return;
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *tracee=nas_task_at(i);
        if(tracee->ptrace_tracer_pid!=tracer_pid ||
           tracee->state==NAS_TASK_FREE || tracee->state==NAS_TASK_ZOMBIE ||
           !(tracee->ptrace_options&NAS_PTRACE_OPT_EXITKILL))continue;
        /* One SIGKILL per thread group is enough. At this point the
         * tracer is exiting, so the target cannot reenter ptrace. */
        tracee->ptrace_tracer_pid=0;
        (void)nas_signal_kill(tracee->tgid,NAS_SIGKILL,0);
    }
}
static bool ptrace_owner_valid(const struct nas_task *task) {
    return task && task->ptrace_tracer_pid &&
        nas_task_by_pid(task->ptrace_tracer_pid)!=0;
}
static void ptrace_stop(struct nas_task *task,uint8_t phase) {
    task->ptrace_phase=phase;
    task->ptrace_event=0;
    task->resume_valid=true;
    task->stopped_from=NAS_TASK_READY;
    task->state=NAS_TASK_STOPPED;
    task->stop_signal=NAS_SIGTRAP |
        ((task->ptrace_options&PTRACE_O_TRACESYSGOOD)?0x80U:0U);
    task->stop_report_pending=true;
    task->continue_report_pending=false;
    /* Real event-driven GDB uses a SIGCHLD handler/self-pipe. A wait4
     * event alone is NOT enough when its thread is blocked in poll. */
    nas_signal_ptrace_stop(task);
}
bool nas_ptrace_user_exception(struct nas_task *task,unsigned signo,
                                uint64_t user_pc,uint64_t user_sp) {
    if(!ptrace_owner_valid(task) || task->state!=NAS_TASK_RUNNING ||
       (signo!=NAS_SIGTRAP && signo!=11U))
        return false;
    task->ptrace_ip=user_pc;
    task->ptrace_sp=user_sp;
    task->ptrace_result=0;

    ptrace_stop(task,0);

    /* The debug/trap event is a SIGTRAP or SIGSEGV signal-delivery stop,
     * not a syscall-entry stop, ptrace event or guessed errno. Preserve
     * the hardware PC verbatim (x86 INT3 already advances by one). */
    task->stop_signal=signo;
    return true;
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
/* A successful exec replaced the old VM. Park a real new-image context:
 * the old user's syscall instruction must NEVER be replayed. */
bool nas_ptrace_exec_commit(struct nas_task *task,uint64_t entry,
                              uint64_t stack) {
    if(!ptrace_owner_valid(task))return false;
    nas_arch_context_exec(&task->arch,entry,stack);
    task->ptrace_ip=entry;
    task->ptrace_sp=stack;
    task->ptrace_result=0;
    ptrace_stop(task,0);
    task->ptrace_event=(task->ptrace_options&PTRACE_O_TRACEEXEC)
        ?PTRACE_EVENT_EXEC:0;
    task->stop_signal=NAS_SIGTRAP | ((uint32_t)task->ptrace_event<<8);
    task->ptrace_event_message=task->pid;
    task->ptrace_exec_pending_exit=true;
    return true;
}
bool nas_ptrace_exit_event(struct nas_task *task,const uint64_t *call,
                            uint64_t exit_status) {
    if(!ptrace_owner_valid(task) ||
       !(task->ptrace_options&PTRACE_O_TRACEEXIT) ||
       task->ptrace_exit_event_sent)return false;
    nas_arch_save_user_frame(task,call);
    nas_arch_save_fpu(task);
    nas_arch_ptrace_rewind_syscall(&task->arch);
    task->ptrace_ip=call[7];
    task->ptrace_sp=nas_arch_signal_user_sp();
    ptrace_stop(task,0);
    task->ptrace_event=PTRACE_EVENT_EXIT;
    task->stop_signal=NAS_SIGTRAP | (PTRACE_EVENT_EXIT<<8);
    task->ptrace_event_message=(exit_status&255ULL)<<8;
    task->ptrace_exit_event_sent=true;
    return true;
}
/* A successful child creation has two real saved native Task images.
 * Hold parent and new child before the child executes a single user
 * instruction. Existing wait4 passes the event to the original tracer.
 * Never replace the child's actual parent identity with its tracer. */
bool nas_ptrace_fork_created(struct nas_task *parent,struct nas_task *child,
                              unsigned event,const uint64_t *call) {
    if(!ptrace_owner_valid(parent))return false;
    uint64_t opt=event==PTRACE_EVENT_FORK?PTRACE_O_TRACEFORK:
        event==PTRACE_EVENT_VFORK?PTRACE_O_TRACEVFORK:
        event==PTRACE_EVENT_CLONE?PTRACE_O_TRACECLONE:0ULL;
    if(!(parent->ptrace_options&opt))return false;
    child->ptrace_tracer_pid=parent->ptrace_tracer_pid;
    child->ptrace_options=parent->ptrace_options;
    child->ptrace_trace_syscalls=parent->ptrace_trace_syscalls;
    child->ptrace_child_initial_stop=true;
    child->ptrace_phase=0;
    child->ptrace_event=0;
    child->ptrace_skip_entry_once=false;
    child->resume_valid=true;
    nas_arch_set_saved_return(child,0);
    child->stopped_from=NAS_TASK_READY;
    child->state=NAS_TASK_STOPPED;
    child->stop_report_pending=true;
    child->stop_signal=NAS_SIGSTOP;
    child->continue_report_pending=false;
    nas_signal_ptrace_stop(child);

    parent->ptrace_result=child->pid;
    parent->ptrace_ip=call[7];
    parent->ptrace_sp=nas_arch_signal_user_sp();
    ptrace_stop(parent,0);
    parent->ptrace_event=(uint8_t)event;
    parent->ptrace_event_message=child->pid;
    parent->stop_signal=NAS_SIGTRAP | (event<<8);
    parent->ptrace_fork_pending_exit=true;
    return true;
}
/* A parked syscall was replayed under the selected Task's real MMU.
 * Its saved native registers remain authoritative: never interpret
 * another Task's current live trap frame as the completing call. */
bool nas_ptrace_blocked_completed(struct nas_task *task,uint64_t value) {
    if(!task->ptrace_blocked_exit_pending)return false;
    task->ptrace_blocked_exit_pending=false;
    if(!task->ptrace_trace_syscalls || !ptrace_owner_valid(task))return false;
    task->ptrace_result=value;
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
/* Linux AArch64 NT_PRFPREG (2): user_fpsimd_state is exactly
 * 32 x 128-bit V registers, FPSR, FPCR, and two reserved u32 values.
 * The Task-owned SIMD snapshot is saved on the real trap boundary.
 * Do NOT invent generic floating point register behavior on x86/RV64. */
#if defined(__aarch64__)
#define NAS_NT_PRFPREG 2ULL
struct nas_linux_arm_fpsimd {
    uint8_t vregs[512];
    uint32_t fpsr,fpcr,reserved[2];
};
_Static_assert(sizeof(struct nas_linux_arm_fpsimd)==528,
               "Linux AArch64 NT_PRFPREG ABI");
static uint64_t ptrace_arm_fpsimd(struct nas_task *task,uint64_t req,
                                  uint64_t data) {
    uint64_t iov[2];
    if(!nas_user_read_bytes(data,(uint8_t *)iov,sizeof(iov)))
        return (uint64_t)-EFAULT;
    struct nas_linux_arm_fpsimd state={0};
    if(req==PTRACE_GETREGSET) {
        memcpy(state.vregs,task->arch.simd_regs,sizeof(state.vregs));
        state.fpsr=(uint32_t)task->arch.fpsr;
        state.fpcr=(uint32_t)task->arch.fpcr;
        uint64_t n=iov[1]<sizeof(state)?iov[1]:sizeof(state);
        if(n && !nas_user_write_bytes(iov[0],(const uint8_t *)&state,n))
            return (uint64_t)-EFAULT;
        return nas_user_write_u64(data+8,n)?0:(uint64_t)-EFAULT;
    }
    if(req==PTRACE_SETREGSET) {
        if(iov[1]<sizeof(state))return (uint64_t)-EINVAL;
        if(!nas_user_read_bytes(iov[0],(uint8_t *)&state,sizeof(state)))
            return (uint64_t)-EFAULT;
        if(!nas_user_write_u64(data+8,sizeof(state)))
            return (uint64_t)-EFAULT;
        memcpy(task->arch.simd_regs,state.vregs,sizeof(state.vregs));
        task->arch.fpsr=state.fpsr;
        task->arch.fpcr=state.fpcr;
        return 0;
    }
    return (uint64_t)-EOPNOTSUPP;
}
#endif
static uint64_t ptrace_gprs(const struct nas_task *task,
                           uint64_t request,uint64_t addr,uint64_t data) {
#if defined(__aarch64__)
    if(request==PTRACE_GETREGSET && addr==NAS_NT_PRFPREG)
        return ptrace_arm_fpsimd((struct nas_task *)task,request,data);
#endif
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
static uint64_t ptrace_set_gprs(struct nas_task *task,
                              uint64_t req,uint64_t addr,uint64_t data) {
#if defined(__aarch64__)
    if(req==PTRACE_SETREGSET && addr==NAS_NT_PRFPREG)
        return ptrace_arm_fpsimd(task,req,data);
#endif
    uint64_t words[34]={0};
    unsigned n=nas_arch_ptrace_gpr_words(task,words);
    uint64_t len=(uint64_t)n*8;
    if(req==PTRACE_SETREGSET) {
        if(addr!=NT_PRSTATUS)return (uint64_t)-EINVAL;
        uint64_t iov[2];
        if(!nas_user_read_bytes(data,(uint8_t *)iov,sizeof(iov)))
            return (uint64_t)-EFAULT;
        if(iov[1]<len)return (uint64_t)-EINVAL;
        if(!nas_user_read_bytes(iov[0],(uint8_t *)words,len))
            return (uint64_t)-EFAULT;
        /* Linux SETREGSET may shorten iov_len on successful requests. */
        if(!nas_arch_ptrace_set_gpr_words(task,words,n))
            return (uint64_t)-EINVAL;
        return nas_user_write_u64(data+8,len)?0:(uint64_t)-EFAULT;
    }
#if defined(__x86_64__)
    if(req==PTRACE_SETREGS) {
        if(!nas_user_read_bytes(data,(uint8_t *)words,len))
            return (uint64_t)-EFAULT;
        return nas_arch_ptrace_set_gpr_words(task,words,n)
            ?0:(uint64_t)-EINVAL;
    }
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
    if(request==PTRACE_ATTACH) {
        /* All NaSeed user processes currently share the single real
         * unprivileged UID. Deny same-thread-group self attachment, init,
         * non-task/scheduler state, and simultaneous tracing. Tracer is
         * NOT made target's real parent; wait4 handles traced descendants
         * with distinct ptrace ownership. */
        if(target==self || target->tgid==self->tgid ||
           target->pid==1 || target->ptrace_tracer_pid ||
           target->group_exit_pending)
            return (uint64_t)-EPERM;
        if(!target->resume_valid ||
           (target->state!=NAS_TASK_READY &&
            target->state!=NAS_TASK_BLOCKED))
            return (uint64_t)-EBUSY;
        target->ptrace_tracer_pid=self->pid;
        target->ptrace_options=0;
        target->ptrace_trace_syscalls=false;
        target->ptrace_phase=0;
        target->ptrace_event=0;
        target->ptrace_event_message=0;
        target->ptrace_exec_pending_exit=false;
        target->ptrace_fork_pending_exit=false;
        target->ptrace_exit_event_sent=false;
        target->ptrace_skip_entry_once=false;
        target->ptrace_result=0;
        target->ptrace_ip=nas_arch_ptrace_saved_ip(&target->arch);
        target->ptrace_sp=nas_arch_ptrace_saved_sp(&target->arch);
        target->ptrace_blocked_exit_pending=target->state==NAS_TASK_BLOCKED &&
                                            target->wait.pending_valid;
        target->stopped_from=target->state;
        target->state=NAS_TASK_STOPPED;
        /* Linux PTRACE_ATTACH delivers an actual SIGSTOP wait event.
         * This is NOT a syscall-entry or exit stop. */
        target->stop_signal=NAS_SIGSTOP;
        target->stop_report_pending=true;
        target->continue_report_pending=false;
        nas_signal_ptrace_stop(target);
        return 0;
    }
    if(target->ptrace_tracer_pid!=self->pid)return (uint64_t)-EPERM;
    if(target->state!=NAS_TASK_STOPPED ||
       target->stopped_from==NAS_TASK_FREE)return (uint64_t)-ESRCH;
    switch(request) {
        case PTRACE_SETOPTIONS:
            /* No fake TRACEFORK/TRACEEXEC/EXITKILL: those events must
             * actually be produced before advertising their options. */
            if(data & ~(PTRACE_O_TRACESYSGOOD|PTRACE_O_TRACEEXEC|
                        PTRACE_O_TRACEEXIT|PTRACE_O_TRACEFORK|
                        PTRACE_O_TRACEVFORK|PTRACE_O_TRACECLONE|
                        PTRACE_O_TRACESECCOMP|PTRACE_O_EXITKILL))
                return (uint64_t)-EINVAL;
            target->ptrace_options=data;
            return 0;
        case PTRACE_GETEVENTMSG:
            return nas_user_write_u64(data,target->ptrace_event_message)
                ? 0:(uint64_t)-EFAULT;
        case PTRACE_GET_SYSCALL_INFO:
            return ptrace_syscall_info(target,addr,data);
        case PTRACE_SETREGSET:
        case PTRACE_SETREGS:
            return ptrace_set_gprs(target,request,addr,data);
        case PTRACE_GETREGSET:
        case PTRACE_GETREGS:
            return ptrace_gprs(target,request,addr,data);
        case PTRACE_POKETEXT:
        case PTRACE_POKEDATA: {
            /* Linux ptrace word writes use DATA as the actual 64-bit value,
             * not a tracer pointer. Required for software breakpoints. */
            int64_t n=nas_vm_write_task_bytes(target,addr,
                                               (const uint8_t *)&data,8);
            if(n<0)return (uint64_t)n;
            return n==8?0:(uint64_t)-EFAULT;
        }
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
            target->ptrace_child_initial_stop=false;
            if(target->ptrace_fork_pending_exit) {
                target->ptrace_fork_pending_exit=false;
                if(request==PTRACE_SYSCALL) {
                    ptrace_stop(target,SYSINFO_EXIT);
                    return 0;
                }
            }
            if(target->ptrace_exec_pending_exit) {
                target->ptrace_exec_pending_exit=false;
                if(request==PTRACE_SYSCALL) {
                    /* Real execve has committed: expose exit-stop before
                     * the new ELF executes a single user instruction. */
                    ptrace_stop(target,SYSINFO_EXIT);
                    target->ptrace_result=0;
                    return 0;
                }
            }
            if(target->ptrace_phase==SYSINFO_ENTRY ||
               target->ptrace_event==PTRACE_EVENT_EXIT)
                target->ptrace_skip_entry_once=true;
            target->ptrace_phase=0;
            target->ptrace_event=0;
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

/* Bounded ARM64 debug only while diagnosing the real GNU GDB monitor path.
 * No synthetic event, no change to ptrace syscall result. */
