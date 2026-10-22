#include <stdint.h>
#include "../errno.h"
#include "../task.h"
#include "../mm/usercopy.h"
#include "signal.h"
#include "../arch/arch.h"
#include "../task/sched.h"
#include "../lib/string.h"
#include "../arch/mmu.h"
#include "../panic.h"
#include <stddef.h>

/* Shared signal disposition/queue. Native caught handlers are enabled
 * only when their ISA frame is real, not by pretending kill() ran a C
 * callback on a kernel stack. Other ISA caught delivery stays explicit. */
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
           task->signal_actions[sig][0]==0)
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
            nas_current_task->signal_actions[signum][0],
            nas_current_task->signal_actions[signum][1],
            nas_current_task->signal_actions[signum][3],
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
           (const uint8_t *)nas_current_task->signal_actions[signum],32))
        return (uint64_t)-EFAULT;
#endif
    if(incoming) {
        for(unsigned i=0;i<4;i++)
            nas_current_task->signal_actions[signum][i]=action[i];
        if(action[0]==1) /* ignored signal drops any earlier pending copy */
            nas_current_task->pending_signals &= ~(1ULL<<(signum-1));
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
        mask &= ~((1ULL<<8)|(1ULL<<18)); /* SIGKILL, SIGSTOP unmaskable */
        /* A pending caught signal needs a real user signal frame: do not
         * claim success, nor lose the blocked pending signal. */
        uint64_t unmasked=task->pending_signals & ~mask;
        for(unsigned sig=1;sig<=64;sig++) {
            if(!(unmasked & (1ULL<<(sig-1))))continue;
            if(task->signal_actions[sig][0]>1) {
                /* All supported ISAs now own a native frame provider. */
            }
        }
    }
    if(previous && !nas_user_write_u64(previous,task->blocked_signals))
        return (uint64_t)-EFAULT;
    if(set) {
        task->blocked_signals=mask;
        unsigned signum=first_default_pending(task,mask);
        if(signum) {
            task->pending_signals &= ~(1ULL<<(signum-1));
            nas_sched_child_exit(0,signum);
            return nas_task_schedule(frame);
        }
    }
    return 0;
}

/* Linux pid_t is signed32 even in LP64 x86, RV64, and ARM64.
 * Signal-zero, unmaskable SIGKILL/SIGSTOP, and SIGCONT's compulsory
 * continuation side effect are shared across the three Linux ABIs.
 * Unknown signal-delivery semantics cannot fake a user-handler frame. */
static bool signal_target_matches(const struct nas_task *target, int64_t pid,
                                   const struct nas_task *sender) {
    if (!target || target->state == NAS_TASK_FREE) return false;
    if (pid > 0) return target->pid == (uint64_t)pid;
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
    nas_sched_child_exit(0, signal);
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
        if (!signal_target_matches(target,pid,sender)) continue;
        if (signal && target->pid==1) {
            protected_init=true; /* never terminate the OS supervisor */
            continue;
        }
        matches[count++]=target;
    }
    if (!count) return (uint64_t)(protected_init ? -EPERM : -ESRCH);
    if (!signal) return 0;
    /* A process-group kill must not partially terminate siblings if one
     * requires a custom user handler that the kernel cannot deliver.
     * SIGKILL/SIGSTOP remain unmaskable and uncatchable. */
    if(signal!=NAS_SIGKILL && signal!=NAS_SIGSTOP && signal!=NAS_SIGCONT) {
        for(unsigned i=0;i<count;i++) {
            struct nas_task *t=matches[i];
            if(t->state==NAS_TASK_ZOMBIE)continue;
            if((t->signal_actions[signal][0]>1) &&
               !(t->blocked_signals&(1ULL<<(signal-1)))) {
                /* All supported ISAs now own a native frame provider. */
            }
        }
    }
    /* Snapshot before state transitions. A STOPPED task preserves its
     * previous READY/BLOCKED syscall resume contract. On CONT it must
     * either become runnable or remain waiting on its original FD.
     * No CPU-specific signal stack or fake success path is introduced. */
    for (unsigned i=0;i<count;i++) {
        struct nas_task *target=matches[i];
        if (target==sender) {
            if (target->signal_actions[signal][0]>1 &&
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
                     target->signal_actions[signal][0]!=1))
                self_terminate=true;
            continue;
        }
        if (target->state==NAS_TASK_ZOMBIE) continue;
        if(terminating_default((unsigned)signal)) {
            if(signal!=NAS_SIGKILL &&
               target->signal_actions[signal][0]==1)continue; /* SIG_IGN */
            if(signal!=NAS_SIGKILL &&
               (target->blocked_signals&(1ULL<<(signal-1)))) {
                target->pending_signals |= 1ULL<<(signal-1);
                continue;
            }
            if(target->signal_actions[signal][0]>1 &&
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
            nas_sched_child_exit(0,(unsigned)signal);
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

/* Only default/ignored terminal signals; custom handlers require real
 * cross-architecture user signal frames and blocked signals need queues.
 * Reject the entire group atomically rather than claim partial delivery. */
bool nas_signal_tty_event(uint64_t sid,uint64_t pgid,unsigned signum) {
    if(!sid || !pgid || (signum!=NAS_SIGINT && signum!=NAS_SIGQUIT &&
                         signum!=NAS_SIGTSTP))return false;
    struct nas_task *targets[NAS_TASK_LIMIT];
    unsigned count=0;
    for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
        struct nas_task *task=nas_task_at(i);
        if(task->state==NAS_TASK_FREE || task->state==NAS_TASK_ZOMBIE ||
           task->sid!=sid || task->pgid!=pgid || task->pid==1)continue;
        if(task->signal_actions[signum][0]>1 ||
           (task->blocked_signals&(1ULL<<(signum-1))))return false;
        targets[count++]=task;
    }
    for(unsigned i=0;i<count;i++) {
        struct nas_task *task=targets[i];
        if(task->signal_actions[signum][0]==1)continue; /* ignored */
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
    if(self->signal_actions[signum][0]==1 ||
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
        if(t->signal_actions[signum][0]>1 &&
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
        if(t->signal_actions[signum][0]==1 ||
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

/* Keep signal frames in a complete native user-return context. The single
 * outstanding frame constraint is intentional until recursive SA_ONSTACK
 * and untrusted multi-frame validation are complete. Other caught signals
 * remain pending rather than corrupting a saved first handler frame. */
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
        if((bits & (1ULL<<(sig-1))) && task->signal_actions[sig][0]>1)
            return true;
    return false;
}

#if defined(__x86_64__)
#include "../arch/x86_64/irq.h"
#include "../arch/x86_64/context.h"
/* Native Linux amd64 struct sigcontext; offsets agree with
 * arch/x86/include/uapi/asm/sigcontext.h. No libc headers in the kernel. */
struct x86_sigcontext {
    uint64_t gpr[18];
    uint16_t cs,gs,fs,ss;
    uint64_t err,trapno,oldmask,cr2,fpstate,reserved[8];
};
struct x86_ucontext {
    uint64_t flags,link;
    struct { uint64_t sp; uint32_t flags,pad; uint64_t size; } stack;
    struct x86_sigcontext sc;
    uint64_t sigmask;
};
struct x86_rt_sigframe {
    uint64_t pretcode;
    struct x86_ucontext uc;
    uint32_t info[32]; /* 128-byte native siginfo_t */
};
_Static_assert(sizeof(struct x86_sigcontext)==256,"Linux x86 sigcontext");
_Static_assert(sizeof(struct x86_ucontext)==304,"Linux x86 ucontext header");
_Static_assert(sizeof(struct x86_rt_sigframe)==440,"Linux x86 rt_sigframe");
_Static_assert(offsetof(struct x86_rt_sigframe,info)==312,"Linux siginfo pointer");
static bool x86_install_handler(uint64_t *regs,unsigned signo,
                                const uint64_t *action) {
    const uint64_t flags=action[1],restorer=action[2],handler=action[0];
    if(!(flags & 0x04000000ULL) || !restorer || restorer>=USER_LIMIT ||
       handler>=USER_LIMIT)return false; /* x86-64 SA_RESTORER required */
    struct nas_task *t=nas_current_task;
    struct nas_arch_context save=t->arch;
    for(unsigned i=0;i<15;i++)save.resume_regs[i]=regs[i];
    save.resume_user_rip=regs[15];
    save.resume_user_rflags=regs[17];
    save.resume_user_rsp=regs[18];
    nas_arch_save_fpu(t);
    for(unsigned i=0;i<sizeof(save.fpu_state);i++)
        save.fpu_state[i]=t->arch.fpu_state[i];
    /* Linux AMD64's 128-byte red zone must be preserved for an
     * asynchronous signal even when a register-only leaf is interrupted. */
    uint64_t sp=save.resume_user_rsp;
    if(sp<128+512+440+24 || sp>=USER_LIMIT)return false;
    uint64_t fpaddr=(sp-128-512)&~UINT64_C(15);
    uint64_t frameaddr=((fpaddr-sizeof(struct x86_rt_sigframe))&
                        ~UINT64_C(15))-8ULL;
    struct x86_rt_sigframe uf={0};
    uf.pretcode=restorer;
    uf.uc.flags=0x6; /* UC_SIGCONTEXT_SS | UC_STRICT_RESTORE_SS */
    uf.uc.sigmask=t->blocked_signals;
    uf.uc.sc.gpr[0]=regs[5];  /* r8 */
    uf.uc.sc.gpr[1]=regs[6];  /* r9 */
    uf.uc.sc.gpr[2]=regs[4];  /* r10 */
    uf.uc.sc.gpr[3]=regs[8];  /* r11 */
    uf.uc.sc.gpr[4]=regs[11]; /* r12 */
    uf.uc.sc.gpr[5]=regs[12]; /* r13 */
    uf.uc.sc.gpr[6]=regs[13]; /* r14 */
    uf.uc.sc.gpr[7]=regs[14]; /* r15 */
    uf.uc.sc.gpr[8]=regs[1];  /* di */
    uf.uc.sc.gpr[9]=regs[2];  /* si */
    uf.uc.sc.gpr[10]=regs[10];/* bp */
    uf.uc.sc.gpr[11]=regs[9]; /* bx */
    uf.uc.sc.gpr[12]=regs[3]; /* dx */
    uf.uc.sc.gpr[13]=regs[0]; /* ax */
    uf.uc.sc.gpr[14]=regs[7]; /* cx */
    uf.uc.sc.gpr[15]=regs[18];/* sp */
    uf.uc.sc.gpr[16]=regs[15];/* ip */
    uf.uc.sc.gpr[17]=regs[17];/* flags */
    uf.uc.sc.cs=0x23U;
    uf.uc.sc.ss=0x1bU;
    uf.uc.sc.fpstate=fpaddr;
    uf.info[0]=(uint32_t)signo; /* si_signo */
    /* FP state is a real FXSAVE image from the exact selected task, not
     * another task's stack. Written before modifying any saved CPU state. */
    if(!nas_user_write_bytes(fpaddr,save.fpu_state,512) ||
       !nas_user_write_bytes(frameaddr,(const uint8_t *)&uf,sizeof(uf)))
        return false;
    t->signal_saved_arch=save;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=frameaddr;
    t->signal_frame_active=true;
    t->blocked_signals |= (1ULL<<(signo-1)) | action[3];
    if(flags & 0x40000000ULL) t->signal_actions[signo][0]=0; /* SA_RESETHAND */
    regs[0]=0;
    regs[1]=signo;
    regs[2]=frameaddr+offsetof(struct x86_rt_sigframe,info);
    regs[3]=frameaddr+offsetof(struct x86_rt_sigframe,uc);
    regs[15]=handler;
    regs[18]=frameaddr;
    return true;
}

#elif defined(__riscv) && __riscv_xlen==64
#include "../arch/riscv64/context.h"
/* Linux arch/riscv/include/uapi/asm/{ptrace,sigcontext,ucontext}.h.
 * Native rt_sigframe is [siginfo:128][ucontext:960], 16-byte aligned.
 * LP64D sigcontext starts at uc+176; the FP union reserves 528 bytes
 * for future extensions even without vector support. */
struct rv_sigcontext {
    uint64_t pc,regs[31];
    struct {
        uint64_t fp[32];
        uint32_t fcsr;
        uint8_t reserved[528-32*8-4];
    } __attribute__((aligned(16))) fp;
};
struct rv_ucontext {
    uint64_t flags,link;
    struct {uint64_t sp;uint32_t flags,pad;uint64_t size;} stack;
    uint64_t sigmask;
    uint8_t unused[120];
    struct rv_sigcontext mc;
};
struct rv_sigframe {uint32_t info[32];struct rv_ucontext uc;};
_Static_assert(sizeof(struct rv_sigcontext)==784,"Linux RV64 sigcontext");
_Static_assert(offsetof(struct rv_ucontext,mc)==176,"Linux RV64 uc_mcontext");
_Static_assert(sizeof(struct rv_ucontext)==960,"Linux RV64 ucontext");
_Static_assert(sizeof(struct rv_sigframe)==1088,"Linux RV64 rt_sigframe");
/* A tiny RX vDSO equivalent, mapped into EACH process address space on
 * exec and initial boot. RA never points to a non-executable stack page. */
#define NAS_SIGRETURN_TRAMPOLINE UINT64_C(0x6ff00000)
static bool rv_install_handler(uint64_t *opaque,unsigned signo,
                               const uint64_t *action) {
    struct nas_rv_trap_frame *fr=(struct nas_rv_trap_frame *)(void *)opaque;
    struct nas_task *t=nas_current_task;
    uint64_t handler=action[0];
    if(!handler || handler>=USER_LIMIT || (handler & 1U))return false;
    nas_arch_save_user_frame(t,opaque);
    nas_arch_save_fpu(t);
    struct nas_arch_context saved=t->arch;
    if(fr->x[2]<sizeof(struct rv_sigframe)+32 ||
       fr->x[2]>=USER_LIMIT)return false;
    uint64_t sp=(fr->x[2]-sizeof(struct rv_sigframe))&~UINT64_C(15);
    struct rv_sigframe sf={0};
    sf.info[0]=(uint32_t)signo;
    sf.uc.sigmask=t->blocked_signals;
    sf.uc.mc.pc=saved.user_pc;
    for(unsigned i=1;i<32;i++)sf.uc.mc.regs[i-1]=saved.user_regs[i];
    for(unsigned i=0;i<32;i++)sf.uc.mc.fp.fp[i]=saved.fp_regs[i];
    sf.uc.mc.fp.fcsr=(uint32_t)saved.fp_fcsr;
    if(!nas_user_write_bytes(sp,(const uint8_t *)&sf,sizeof(sf)))return false;
    t->signal_saved_arch=saved;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=sp;
    t->signal_frame_active=true;
    t->blocked_signals |= (1ULL<<(signo-1))|action[3];
    if(action[1] & 0x40000000ULL)t->signal_actions[signo][0]=0;
    fr->x[2]=sp;
    fr->x[1]=NAS_SIGRETURN_TRAMPOLINE; /* return address, not user stack */
    fr->x[10]=signo;
    fr->x[11]=sp+offsetof(struct rv_sigframe,info);
    fr->x[12]=sp+offsetof(struct rv_sigframe,uc);
    fr->sepc=handler;
    return true;
}
#elif defined(__aarch64__)
#include "../arch/aarch64/context.h"
/* Linux arm64 uapi sigcontext: 31 GPR, SP/PC/PSTATE, then 4KiB
 * aligned extension buffer with FPSIMD_MAGIC, size=528 and null terminator. */
struct arm_sigcontext {
    uint64_t fault_address,regs[31],sp,pc,pstate;
    uint8_t reserved[4096] __attribute__((aligned(16)));
};
struct arm_ucontext {
    uint64_t flags,link;
    struct {uint64_t sp;uint32_t flags,pad;uint64_t size;} stack;
    uint64_t sigmask;
    uint8_t unused[120];
    struct arm_sigcontext mc;
};
struct arm_sigframe {uint32_t info[32];struct arm_ucontext uc;};
struct arm_fpsimd {
    uint32_t magic,size,fpsr,fpcr;
    uint8_t regs[512] __attribute__((aligned(16)));
};
_Static_assert(offsetof(struct arm_sigcontext,reserved)==288,
               "Linux ARM64 FPSIMD reserved offset");
_Static_assert(sizeof(struct arm_sigcontext)==4384,
               "Linux ARM64 sigcontext");
_Static_assert(offsetof(struct arm_ucontext,mc)==176,
               "Linux ARM64 uc_mcontext");
_Static_assert(sizeof(struct arm_sigframe)==4688,
               "Linux ARM64 rt_sigframe");
_Static_assert(sizeof(struct arm_fpsimd)==528,
               "Linux ARM64 fpsimd_context");
#define NAS_SIGRETURN_TRAMPOLINE UINT64_C(0x6ff00000)
static bool arm_install_handler(uint64_t *opaque,unsigned signo,
                                const uint64_t *action) {
    struct nas_arm_trap_frame *fr=(struct nas_arm_trap_frame *)(void *)opaque;
    struct nas_task *t=nas_current_task;
    uint64_t handler=action[0];
    if(!handler || handler>=USER_LIMIT || (handler & 3U))return false;
    nas_arch_save_user_frame(t,opaque);
    nas_arch_save_fpu(t);
    struct nas_arch_context saved=t->arch;
    if(fr->sp_el0<sizeof(struct arm_sigframe)+32 ||
       fr->sp_el0>=USER_LIMIT)return false;
    uint64_t sp=(fr->sp_el0-sizeof(struct arm_sigframe))&~UINT64_C(15);
    struct arm_sigframe sf={0};
    sf.info[0]=(uint32_t)signo;
    sf.uc.sigmask=t->blocked_signals;
    for(unsigned i=0;i<31;i++)sf.uc.mc.regs[i]=saved.user_regs[i];
    sf.uc.mc.sp=saved.user_sp;
    sf.uc.mc.pc=saved.user_elr;
    sf.uc.mc.pstate=saved.user_spsr;
    struct arm_fpsimd *fp=(struct arm_fpsimd *)(void *)sf.uc.mc.reserved;
    fp->magic=0x46508001U;
    fp->size=sizeof(*fp);
    fp->fpsr=(uint32_t)saved.fpsr;
    fp->fpcr=(uint32_t)saved.fpcr;
    for(unsigned i=0;i<512;i++)fp->regs[i]=saved.simd_regs[i];
    if(!nas_user_write_bytes(sp,(const uint8_t *)&sf,sizeof(sf)))return false;
    t->signal_saved_arch=saved;
    t->signal_saved_mask=t->blocked_signals;
    t->signal_frame_sp=sp;
    t->signal_frame_active=true;
    t->blocked_signals |= (1ULL<<(signo-1))|action[3];
    if(action[1] & 0x40000000ULL)t->signal_actions[signo][0]=0;
    fr->sp_el0=sp;
    fr->x[30]=NAS_SIGRETURN_TRAMPOLINE; /* Linux rt_sigreturn via RX page */
    fr->x[0]=signo;
    fr->x[1]=sp+offsetof(struct arm_sigframe,info);
    fr->x[2]=sp+offsetof(struct arm_sigframe,uc);
    fr->elr_el1=handler;
    return true;
}
#endif

void nas_signal_user_return(uint64_t *frame) {
    struct nas_task *t=nas_current_task;
    if(t->signal_frame_active || !frame || !nas_signal_user_pending(t))
        return;
    uint64_t ready=t->pending_signals &
        (~t->blocked_signals | t->signal_force_deliver);
    for(unsigned sig=1;sig<=64;sig++) {
        if(!(ready&(1ULL<<(sig-1))))continue;
        const uint64_t *act=t->signal_actions[sig];
        if(act[0]<=1)continue;
#if defined(__x86_64__)
        bool ok=x86_install_handler(frame,sig,act);
#elif defined(__riscv) && __riscv_xlen==64
        bool ok=rv_install_handler(frame,sig,act);
#elif defined(__aarch64__)
        bool ok=arm_install_handler(frame,sig,act);
#endif
        if(!ok)nas_fail("M30 cannot build native user signal frame");
        t->pending_signals &= ~(1ULL<<(sig-1));
        t->signal_force_deliver &= ~(1ULL<<(sig-1));
        return;
    }
}

uint64_t nas_signal_sigreturn(uint64_t *frame) {
#if defined(__x86_64__)
    struct nas_task *t=nas_current_task;
    if(!t->signal_frame_active ||
       syscall_user_rsp != t->signal_frame_sp+8)
        return (uint64_t)-EFAULT;
    /* The handler's untrusted user frame cannot inject privileged CS,
     * SS or flags into IRET: restore the kernel-held full user snapshot.
     * A future ucontext-mutation implementation will validate each field. */
    t->arch=t->signal_saved_arch;
    t->blocked_signals=t->signal_saved_mask;
    t->signal_frame_active=false;
    t->signal_frame_sp=0;
    nas_arch_restore_cpu(t);
    nas_arch_load_user_frame(t,frame);
    return nas_arch_saved_return(t);
#elif defined(__riscv) && __riscv_xlen==64
    struct nas_task *t=nas_current_task;
    struct nas_rv_trap_frame *tf=(struct nas_rv_trap_frame *)(void *)frame;
    if(!t->signal_frame_active || tf->x[2]!=t->signal_frame_sp)
        return (uint64_t)-EFAULT;
    t->arch=t->signal_saved_arch;
    t->blocked_signals=t->signal_saved_mask;
    t->signal_frame_active=false;
    t->signal_frame_sp=0;
    nas_arch_restore_cpu(t);
    nas_arch_load_user_frame(t,frame);
    return nas_arch_saved_return(t);
#elif defined(__aarch64__)
    struct nas_task *t=nas_current_task;
    struct nas_arm_trap_frame *tf=(struct nas_arm_trap_frame *)(void *)frame;
    if(!t->signal_frame_active || tf->sp_el0!=t->signal_frame_sp)
        return (uint64_t)-EFAULT;
    t->arch=t->signal_saved_arch;
    t->blocked_signals=t->signal_saved_mask;
    t->signal_frame_active=false;
    t->signal_frame_sp=0;
    nas_arch_restore_cpu(t);
    nas_arch_load_user_frame(t,frame);
    return nas_arch_saved_return(t);
#endif
}

/* AArch64 and RV64 ABI hand the handler an executable signal-return
 * address in LR/RA, not a guessed stack trampoline. The tiny mapped
 * code page is read+exec from first map; user writes are forbidden.
 * x86_64 obtains its SA_RESTORER from libc, so needs no kernel mapping. */
bool nas_signal_install_trampoline(void) {
#if defined(__riscv) && __riscv_xlen==64
    static const uint32_t insns[]={
        0x08b00893U, /* addi a7,zero,139 : __NR_rt_sigreturn */
        0x00000073U, /* ecall */
        0x00100073U, /* ebreak: unexpected return fails closed */
    };
#elif defined(__aarch64__)
    static const uint32_t insns[]={
        0xd2801168U, /* movz x8,#139 : __NR_rt_sigreturn */
        0xd4000001U, /* svc #0 */
        0xd4200000U, /* brk #0: unexpected return fails closed */
    };
#else
    return true; /* native libc SA_RESTORER provides x86 restorer */
#endif
#if defined(__riscv) || defined(__aarch64__)
    if(!nas_mmu_map_prot_page(NAS_SIGRETURN_TRAMPOLINE,5U))
        return false;
    if(!nas_user_copy_to_mapped(NAS_SIGRETURN_TRAMPOLINE,
                               (const uint8_t *)insns,sizeof(insns))) {
        (void)nas_mmu_unmap_page(NAS_SIGRETURN_TRAMPOLINE);
        return false;
    }
    return true;
#endif
}
