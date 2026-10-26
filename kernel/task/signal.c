#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../mm/usercopy.h"
#include "signal.h"
#include "../arch/arch.h"
#include "../task/sched.h"
#include "../panic.h"

/* Linux UAPI asm-generic signal.h constants for LP64. 0x40000000 is
 * SA_NODEFER, NOT SA_RESETHAND (0x80000000). All three ISAs share them. */
#define NAS_SA_NODEFER UINT64_C(0x40000000)
#define NAS_SA_RESETHAND UINT64_C(0x80000000)
#define NAS_SA_ONSTACK UINT64_C(0x08000000)
#define NAS_UNMASKABLE ((1ULL<<8) | (1ULL<<18))

/* Shared signal lifecycle. Hardware-specific sigframe assembly is confined
 * to the three native backends below; disposition and mask decisions must
 * agree across all user-visible entry, fork, TTY and pipe paths. */
static bool terminating_default(unsigned sig) {
    return sig==NAS_SIGINT || sig==NAS_SIGQUIT || sig==NAS_SIGKILL ||
           sig==NAS_SIGPIPE || sig==NAS_SIGTERM ||
           sig==NAS_SIGUSR1 || sig==NAS_SIGUSR2;
}
static unsigned first_default_pending(const struct nas_task *task,
                                      uint64_t mask) {
    uint64_t ready=task->pending_signals & ~mask;
    for(unsigned sig=1;sig<=64;sig++)
        if((ready & (1ULL<<(sig-1))) && terminating_default(sig) &&
           task->sighand->actions[sig][0]==0)
            return sig;
    return 0;
}
uint64_t nas_signal_action(uint64_t signum,uint64_t incoming,
                           uint64_t previous,uint64_t size) {
    if(size!=8 || signum<1 || signum>64)return (uint64_t)-EINVAL;
    if(incoming && (signum==NAS_SIGKILL || signum==NAS_SIGSTOP))
        return (uint64_t)-EINVAL;
    uint64_t action[4]={0};
    /* Linux asm-generic RV64 has NO sa_restorer field: kernel rt_sigaction
     * is handler/flags/sigmask (24 bytes). x86/ARM64 have 4 words with
     * a native restorer. Canonical in-kernel order remains H/F/R/M. */
#if defined(__riscv) && __riscv_xlen==64
    uint64_t rv_wire[3]={0};
    if(incoming) {
        if(!nas_user_read_bytes(incoming,(uint8_t *)rv_wire,sizeof(rv_wire)))
            return (uint64_t)-EFAULT;
        action[0]=rv_wire[0];action[1]=rv_wire[1];
        action[2]=0;action[3]=rv_wire[2];
    }
    if(previous) {
        uint64_t old[3]={
            nas_current_task->sighand->actions[signum][0],
            nas_current_task->sighand->actions[signum][1],
            nas_current_task->sighand->actions[signum][3],
        };
        if(!nas_user_write_bytes(previous,(const uint8_t *)old,sizeof(old)))
            return (uint64_t)-EFAULT;
    }
#else
    /* Copy before mutation to make overlapping userspace buffers safe. */
    if(incoming && !nas_user_read_bytes(incoming,(uint8_t *)action,32))
        return (uint64_t)-EFAULT;
    if(previous &&
       !nas_user_write_bytes(previous,
           (const uint8_t *)nas_current_task->sighand->actions[signum],32))
        return (uint64_t)-EFAULT;
#endif
    if(incoming) {
        /* Do not silently advertise SA_ONSTACK while sigaltstack and
         * nested native signal frames remain unsupported. */
        if(action[1]&NAS_SA_ONSTACK)return (uint64_t)-EOPNOTSUPP;
        action[3] &= ~NAS_UNMASKABLE;
        for(unsigned i=0;i<4;i++)
            nas_current_task->sighand->actions[signum][i]=action[i];
        if(action[0]==1) { /* ignored signal discards queued copies */
            nas_current_task->pending_signals &= ~(1ULL<<(signum-1));
            nas_current_task->signal_force_deliver &= ~(1ULL<<(signum-1));
        }
    }
    return 0;
}
uint64_t nas_signal_pending(uint64_t destination,uint64_t size) {
    if(size!=8)return (uint64_t)-EINVAL;
    return nas_user_write_u64(destination,
        nas_current_task->pending_signals & nas_current_task->blocked_signals)
        ? 0:(uint64_t)-EFAULT;
}
uint64_t nas_signal_procmask(uint64_t how,uint64_t set,
                             uint64_t previous,uint64_t size,uint64_t *frame) {
    if(size!=8)return (uint64_t)-EINVAL;
    uint64_t newbits=0;
    struct nas_task *task=nas_current_task;
    if(set && !nas_user_read_u64(set,&newbits))return (uint64_t)-EFAULT;
    uint64_t mask=task->blocked_signals;
    if(set) {
        if(how==0)mask |= newbits; /* SIG_BLOCK */
        else if(how==1)mask &= ~newbits; /* SIG_UNBLOCK */
        else if(how==2)mask=newbits; /* SIG_SETMASK */
        else return (uint64_t)-EINVAL;
        mask &= ~NAS_UNMASKABLE; /* SIGKILL, SIGSTOP unmaskable */
    }
    if(previous && !nas_user_write_u64(previous,task->blocked_signals))
        return (uint64_t)-EFAULT;
    if(set) {
        task->blocked_signals=mask;
        unsigned signum=first_default_pending(task,mask);
        if(signum) {
            task->pending_signals &= ~(1ULL<<(signum-1));
            nas_sched_thread_group_exit(0,signum);
            return nas_task_schedule(frame);
        }
    }
    return 0;
}

/* Thread-directed Linux signal ABI: exact TID targeting with TGID
 * verification. Masked/caught signals are queued for ONLY that thread;
 * default-fatal disposition terminates the WHOLE Linux thread group.
 * Never inspect another task's userspace pointers before installing
 * its actual page-table root, including futex/robust TID cleanup. */
static uint64_t signal_send_thread(uint64_t raw_tgid,uint64_t raw_tid,
                                    uint64_t signo,uint64_t *frame,
                                    bool verify_tgid) {
    int64_t tid=(int64_t)(int32_t)(uint32_t)raw_tid;
    int64_t tgid=(int64_t)(int32_t)(uint32_t)raw_tgid;
    if(tid<=0 || (verify_tgid && tgid<=0))return (uint64_t)-EINVAL;
    if(signo>64)return (uint64_t)-EINVAL;
    struct nas_task *target=nas_task_by_pid((uint64_t)tid);
    if(!target || target->state==NAS_TASK_ZOMBIE ||
       (verify_tgid && target->tgid!=(uint64_t)tgid))
        return (uint64_t)-ESRCH;
    if(!signo)return 0;
    if(target->tgid==1)return (uint64_t)-EPERM;
    if(signo!=NAS_SIGSTOP && signo!=NAS_SIGCONT &&
       !terminating_default((unsigned)signo))
        return (uint64_t)-EOPNOTSUPP;
    if(signo==NAS_SIGSTOP || signo==NAS_SIGCONT)
        return nas_signal_kill(target->tgid,signo,frame);
    uint64_t disp=target->sighand->actions[signo][0];
    uint64_t bit=1ULL<<(signo-1);
    if(signo!=NAS_SIGKILL) {
        if(disp==1)return 0;
        if(disp>1 || (target->blocked_signals&bit)) {
            target->pending_signals|=bit;
            return 0;
        }
    }
    /* Delivery of an unhandled fatal *thread-directed* signal kills
     * the whole group. Killing a thread must NOT leave its leader alive. */
    if(nas_current_task->tgid==target->tgid) {
        nas_sched_thread_group_exit(0,(uint32_t)signo);
        return nas_task_schedule(frame);
    }
    struct nas_task *sender=nas_current_task;
    nas_arch_save_fpu(sender);
    nas_current_task=target;
    nas_arch_restore_cpu(target);
    nas_sched_thread_group_exit(0,(uint32_t)signo);
    nas_current_task=sender;
    nas_arch_restore_cpu(sender);
    return 0;
}
uint64_t nas_signal_tkill(uint64_t raw_tid,uint64_t signo,uint64_t *frame) {
    return signal_send_thread(0,raw_tid,signo,frame,false);
}
uint64_t nas_signal_tgkill(uint64_t raw_tgid,uint64_t raw_tid,
                           uint64_t signo,uint64_t *frame) {
    return signal_send_thread(raw_tgid,raw_tid,signo,frame,true);
}

/* Linux pid_t is signed32 even in LP64 x86, RV64, and ARM64.
 * Signal-zero, unmaskable SIGKILL/SIGSTOP, and SIGCONT's compulsory
 * continuation side effect are shared across the three Linux ABIs.
 * Unknown signal-delivery semantics cannot fake a user-handler frame. */
static bool signal_target_matches(const struct nas_task *target, int64_t pid,
                                   const struct nas_task *sender,
                                   bool include_threads) {
    /* kill(pid,SIGKILL) selects process leaders, never raw pthread TIDs.
     * Job-control STOP/CONT, by contrast, stop/resume every live TID in
     * the selected process group, not just the group leader. */
    if (!target || target->state == NAS_TASK_FREE ||
        (target->is_thread && !include_threads))
        return false;
    if (pid > 0) return target->tgid == (uint64_t)pid;
    if (pid == 0) return target->pgid == sender->pgid;
    if (pid == -1) return target != sender && target->pid != 1;
    return target->pgid == (uint64_t)-pid;
}
/* Common cooperative process exit owns fd and pipe last-close, PID1
 * adoption, child TID clear and zombie state. On remote kill, the
 * target's VM MUST be active when clearing child_tid user memory.
 * The active caller's FPU, MMU and TLS are restored before continuing.
 * No kernel task stacks or architecture-specific process tables added. */
static void signal_kill_other(struct nas_task *target,unsigned signal) {
    struct nas_task *sender = nas_current_task;
    nas_arch_save_fpu(sender);
    nas_current_task = target;
    nas_arch_restore_cpu(target);
    nas_sched_thread_group_exit(0, signal);
    nas_current_task = sender;
    nas_arch_restore_cpu(sender);
}
uint64_t nas_signal_kill(uint64_t raw_pid, uint64_t signal, uint64_t *frame) {
    int64_t pid = (int64_t)(int32_t)(uint32_t)raw_pid;
    if (signal > 64) return (uint64_t)-EINVAL;
    if(signal && signal!=NAS_SIGSTOP && signal!=NAS_SIGCONT &&
       !terminating_default((unsigned)signal))
        return (uint64_t)-EOPNOTSUPP; /* no fake signal delivery */
    struct nas_task *sender = nas_current_task;
    struct nas_task *matches[NAS_TASK_LIMIT];
    unsigned count = 0;
    bool protected_init = false, self = false;
    bool self_terminate = false;
    for (unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *target = nas_task_at(i);
        if (!signal_target_matches(target,pid,sender,
                signal==NAS_SIGSTOP || signal==NAS_SIGCONT)) continue;
        if (signal && target->pid==1) {
            protected_init=true; /* never terminate the OS supervisor */
            continue;
        }
        matches[count++]=target;
    }
    if (!count) return (uint64_t)(protected_init ? -EPERM : -ESRCH);
    if (!signal) return 0;
    /* Snapshot before state transitions. A STOPPED task preserves its
     * previous READY/BLOCKED syscall resume contract. On CONT it must
     * either become runnable or remain waiting on its original FD.
     * No CPU-specific signal stack or fake success path is introduced. */
    for (unsigned i=0;i<count;i++) {
        struct nas_task *target=matches[i];
        if (target==sender) {
            if (target->sighand->actions[signal][0]>1 &&
                !(target->blocked_signals&(1ULL<<(signal-1))) &&
                signal!=NAS_SIGKILL && signal!=NAS_SIGSTOP &&
                signal!=NAS_SIGCONT) {
                target->pending_signals |= 1ULL<<(signal-1);
                continue;
            }
            self=true;
            if(terminating_default((unsigned)signal) &&
               signal!=NAS_SIGKILL &&
               (target->blocked_signals&(1ULL<<(signal-1))))
                target->pending_signals |= 1ULL<<(signal-1);
            else if(terminating_default((unsigned)signal) &&
                    (signal==NAS_SIGKILL ||
                     target->sighand->actions[signal][0]!=1))
                self_terminate=true;
            continue;
        }
        if (target->state==NAS_TASK_ZOMBIE || target->state==NAS_TASK_FREE) continue;
        if(terminating_default((unsigned)signal)) {
            if(signal!=NAS_SIGKILL &&
               target->sighand->actions[signal][0]==1)continue; /* SIG_IGN */
            if(signal!=NAS_SIGKILL &&
               (target->blocked_signals&(1ULL<<(signal-1)))) {
                target->pending_signals |= 1ULL<<(signal-1);
                continue;
            }
            if(target->sighand->actions[signal][0]>1 &&
               signal!=NAS_SIGKILL) {
                /* Delivery waits until THIS task's page tables and user
                 * return frame are selected. Do not write a foreign stack. */
                target->pending_signals |= 1ULL<<(signal-1);
                continue;
            }
            signal_kill_other(target,(unsigned)signal);
        } else if (signal==NAS_SIGSTOP) {
            if (target->state != NAS_TASK_STOPPED) {
                target->stopped_from=target->state;
                target->state=NAS_TASK_STOPPED;
                target->stop_report_pending=true;
                target->continue_report_pending=false;
                target->stop_signal=NAS_SIGSTOP;
            }
        } else if (target->state==NAS_TASK_STOPPED) {
            target->state=target->stopped_from==NAS_TASK_BLOCKED
                ? NAS_TASK_BLOCKED : NAS_TASK_READY;
            target->stopped_from=NAS_TASK_FREE;
            target->stop_report_pending=false;
            target->continue_report_pending=true;
        }
    }
    if (self) {
        if(self_terminate) {
            nas_sched_thread_group_exit(0,(unsigned)signal);
            return nas_task_schedule(frame);
        }
        if (signal==NAS_SIGSTOP && sender->state!=NAS_TASK_STOPPED) {
            nas_arch_save_user_frame(sender,frame);
            nas_arch_save_fpu(sender);
            nas_arch_set_saved_return(sender,0);
            sender->resume_valid=true;
            sender->stopped_from=NAS_TASK_READY;
            sender->state=NAS_TASK_STOPPED;
            sender->stop_report_pending=true;
            sender->continue_report_pending=false;
            sender->stop_signal=NAS_SIGSTOP;
            return nas_task_schedule(frame);
        }
        /* SIGCONT to a running sender is a successful no-op. */
    }
    return 0;
}

/* TTY ISIG acts on the foreground group, not on the running kernel Task.
 * Caught or masked signals are queued and delivered on that member's own
 * user-return boundary with its MMU context; default termination and stop
 * reuse the existing Task lifecycle. */
bool nas_signal_tty_event(uint64_t sid,uint64_t pgid,unsigned signum) {
    if(!sid || !pgid || (signum!=NAS_SIGINT && signum!=NAS_SIGQUIT &&
                         signum!=NAS_SIGTSTP))return false;
    struct nas_task *targets[NAS_TASK_LIMIT];
    unsigned count=0;
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *task=nas_task_at(i);
        if(task->state==NAS_TASK_FREE || task->state==NAS_TASK_ZOMBIE ||
           task->sid!=sid || task->pgid!=pgid || task->pid==1)continue;
        /* A masked default SIGTSTP needs full delayed job-control STOP
         * semantics; fail explicitly instead of silently losing it. */
        if(signum==NAS_SIGTSTP &&
           task->sighand->actions[signum][0]==0 &&
           (task->blocked_signals&(1ULL<<(signum-1))))return false;
        targets[count++]=task;
    }
    for(unsigned i=0;i<count;i++) {
        struct nas_task *task=targets[i];
        if(task->sighand->actions[signum][0]==1)continue; /* ignored */
        if(task->sighand->actions[signum][0]>1 ||
           (task->blocked_signals&(1ULL<<(signum-1)))) {
            task->pending_signals |= 1ULL<<(signum-1);
            continue;
        }
        if(signum==NAS_SIGTSTP) {
            if(task->state!=NAS_TASK_STOPPED) {
                task->stopped_from=task->state;
                task->state=NAS_TASK_STOPPED;
                task->stop_report_pending=true;
                task->continue_report_pending=false;
                task->stop_signal=signum;
            }
        } else if(task==nas_current_task) {
            nas_sched_child_exit(0,signum);
        } else signal_kill_other(task,signum);
    }
    return true;
}

/* Linux job control: reads by a non-foreground process group generate
 * SIGTTIN; TOSTOP writes and TIOCSPGRP by background groups generate
 * SIGTTOU. No hardware nor ISA-dependent policy is needed here.
 *
 * This cooperative kernel has one shared kernel stack. A running task
 * cannot be stopped in-place and then "return" from a syscall: capture
 * its live frame, mark STOPPED, and schedule its parent. SIGCONT resumes
 * after the interrupted syscall with EINTR, which is a legitimate
 * restartable user-space boundary. No imaginary signal handler runs. */
uint64_t nas_signal_background_job(unsigned signum,uint64_t *frame,
                                    bool *handled) {
    *handled=false;
    if(signum!=NAS_SIGTTIN && signum!=NAS_SIGTTOU)
        return (uint64_t)-EINVAL;
    struct nas_task *self=nas_current_task;
    if(self->pid==1 || self->pgid==0 || self->sid==0)
        return 0; /* protected init / no controlling session */
    uint64_t bit=1ULL<<(signum-1);
    if(self->sighand->actions[signum][0]==1 ||
       (self->blocked_signals&bit)) {
        /* POSIX: background read without deliverable SIGTTIN fails EIO.
         * SIGTTOU ignored/blocked permits a background writer. */
        if(signum==NAS_SIGTTIN) {
            *handled=true;
            return (uint64_t)-EIO;
        }
        return 0;
    }
    struct nas_task *members[NAS_TASK_LIMIT];
    unsigned count=0;
    bool orphaned=true;
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *t=nas_task_at(i);
        if(t->state==NAS_TASK_FREE || t->state==NAS_TASK_ZOMBIE ||
           t->sid!=self->sid || t->pgid!=self->pgid)continue;
        struct nas_task *parent=nas_task_by_pid(t->ppid);
        if(parent && parent->sid==t->sid && parent->pgid!=t->pgid)
            orphaned=false;
        if(t->sighand->actions[signum][0]>1 &&
           !(t->blocked_signals&bit)) {
            *handled=true;
            return (uint64_t)-EOPNOTSUPP; /* requires real sigframe */
        }
        members[count++]=t;
    }
    if(orphaned || !count) {
        *handled=true;
        return (uint64_t)-EIO; /* POSIX orphaned process-group rule */
    }
    /* Validate all process-group members BEFORE stopping any. */
    bool self_stopped=false;
    for(unsigned i=0;i<count;i++) {
        struct nas_task *t=members[i];
        if(t->sighand->actions[signum][0]==1 ||
           (t->blocked_signals&bit) || t->state==NAS_TASK_STOPPED)
            continue;
        if(t==self) {
            nas_arch_save_user_frame(t,frame);
            nas_arch_save_fpu(t);
            nas_arch_set_saved_return(t,(uint64_t)-EINTR);
            t->resume_valid=true;
            t->stopped_from=NAS_TASK_READY;
            self_stopped=true;
        } else {
            t->stopped_from=t->state;
        }
        t->state=NAS_TASK_STOPPED;
        t->stop_signal=signum;
        t->stop_report_pending=true;
        t->continue_report_pending=false;
    }
    if(self_stopped) {
        *handled=true;
        return nas_task_schedule(frame);
    }
    /* If the caller was already ignored or masked, we returned above.
     * An active background process must either STOP or get a true error. */
    *handled=true;
    return (uint64_t)-EIO;
}

/* Broken pipe needs a real pending SIGPIPE even when blocked, not only an
 * EPIPE return. The default unblocked action remains the existing immediate
 * termination path used by the scheduler; this only queues handler/masked
 * instances for that same Task's subsequent user return. */
void nas_signal_broken_pipe(uint64_t fd) {
    struct nas_task *t=nas_current_task;
    if(!t || !t->ppid)return;
    uint8_t kind=nas_task_fd_kind(t,fd);
    if(kind!=FD_PIPE_W && kind!=FD_SOCKET)return;
    uint64_t disp=t->sighand->actions[NAS_SIGPIPE][0];
    if(disp!=1 && (disp>1 ||
                   (t->blocked_signals&(1ULL<<(NAS_SIGPIPE-1)))))
        t->pending_signals |= 1ULL<<(NAS_SIGPIPE-1);
}

/* Linux automatic blocking and one-shot disposition reset are separate
 * operations: SA_NODEFER controls the current signal's automatic mask,
 * SA_RESETHAND resets all four saved action fields after frame creation.
 * Explicit sa_mask always applies, excluding unmaskable KILL/STOP. */
void nas_signal_handler_enter(struct nas_task *t,unsigned signo,
                                 const uint64_t *act) {
    uint64_t flags=act[1];
    uint64_t bits=act[3]&~NAS_UNMASKABLE;
    if(!(flags&NAS_SA_NODEFER))bits |= 1ULL<<(signo-1);
    t->blocked_signals |= bits;
    if(flags&NAS_SA_RESETHAND)
        for(unsigned i=0;i<4;i++)t->sighand->actions[signo][i]=0;
}

/* Keep signal frames in a complete native user-return context. The single
 * outstanding frame constraint is intentional until recursive SA_ONSTACK
 * and untrusted multi-frame validation are complete. Other caught signals
 * remain pending rather than corrupting a saved first handler frame. */
/* Linux child-exit notification: the zombie and wait4 status remain owned
 * by process.c. A caught SIGCHLD interrupts an actual pselect/ppoll wait
 * so an event-driven parent can reap its children and replenish jobserver
 * pipe tokens. Queue even if masked: pselect6 installs its own temporary
 * signal mask and must observe the pending transition atomically.
 * SIG_DFL/SIG_IGN SIGCHLD cause no signal delivery; wait4 still works. */
void nas_signal_ptrace_stop(const struct nas_task *tracee) {
    if(!tracee || !tracee->ptrace_tracer_pid ||
       !tracee->stop_report_pending)return;
    struct nas_task *tracer=nas_task_by_pid(tracee->ptrace_tracer_pid);
    if(!tracer || tracer->state==NAS_TASK_FREE ||
       tracer->state==NAS_TASK_ZOMBIE)return;
    /* Linux SIGCHLD is pending only if a handler is installed. The
     * waitable ptrace-stop remains independent of this signal, and the
     * signal disposition belongs to the tracer's real process group. */
    const struct nas_task *owner=tracer;
    if(owner->sighand->actions[NAS_SIGCHLD][0]>1)
        tracer->pending_signals|=1ULL<<(NAS_SIGCHLD-1);
}
void nas_signal_child_exit(const struct nas_task *child) {
    if (!child || !child->ppid) return;
    struct nas_task *parent=nas_task_by_pid(child->ppid);
    if (!parent || parent->state==NAS_TASK_FREE ||
        parent->state==NAS_TASK_ZOMBIE) return;
    if (parent->sighand->actions[NAS_SIGCHLD][0] > 1)
        parent->pending_signals |= 1ULL << (NAS_SIGCHLD-1);
}
void nas_signal_ppoll_restore(struct nas_task *task,bool interrupted) {
    if(!task || !task->ppoll_mask_active)return;
    if(interrupted)task->signal_force_deliver |=
        task->pending_signals & ~task->blocked_signals;
    task->blocked_signals=task->ppoll_saved_mask;
    task->ppoll_mask_active=false;
}
bool nas_signal_user_pending(const struct nas_task *task) {
    if(!task || task->signal_frame_active) return false;
    uint64_t bits=task->pending_signals &
        (~task->blocked_signals | task->signal_force_deliver);
    for(unsigned sig=1;sig<=64;sig++)
        if((bits & (1ULL<<(sig-1))) && task->sighand->actions[sig][0]>1)
            return true;
    return false;
}

void nas_signal_user_return(uint64_t *frame) {
    struct nas_task *t=nas_current_task;
    if(t->signal_frame_active || !frame || !nas_signal_user_pending(t))
        return;
    uint64_t ready=t->pending_signals &
        (~t->blocked_signals | t->signal_force_deliver);
    for(unsigned sig=1;sig<=64;sig++) {
        if(!(ready&(1ULL<<(sig-1))))continue;
        const uint64_t *act=t->sighand->actions[sig];
        if(act[0]<=1)continue;
        if(!nas_arch_signal_install_handler(frame,sig,act))
            nas_fail("M30 cannot build native user signal frame");

        t->pending_signals &= ~(1ULL<<(sig-1));
        t->signal_force_deliver &= ~(1ULL<<(sig-1));
        return;
    }
}

uint64_t nas_signal_sigreturn(uint64_t *frame) {
    /* Native provider owns trap-state validation and safe context restore.
     * Common signal state remains owned by the Task, never a guest sigframe. */
    return nas_arch_signal_sigreturn(frame);
}

bool nas_signal_install_trampoline(void) {
    if(!nas_arch_signal_install_trampoline())return false;
    /* x86 uses SA_RESTORER from libc and does not map a helper page.
     * Native RV64/ARM64 install an actual RX user trampoline. */
    if(!nas_mmu_user_byte(NAS_USER_SIGNAL_TRAMPOLINE))return true;
    return nas_vma_record(&nas_current_task->mm->mmap_vmas,
                          NAS_USER_SIGNAL_TRAMPOLINE,4096,5,
                          NAS_VMA_SIGNAL_TRAMPOLINE,0);
}
