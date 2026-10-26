#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../syscall/ops.h"
#include "../task.h"
#include "../fd.h"
#include "../pipe.h"
#include "../arch/arch.h"
#include "../fs/io.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "process.h"
#include "sched.h"
#include "wait.h"
#include "futex_wait.h"
#include "blocked_completion.h"
#include "kthread.h"
#include "scheduler_service.h"
#include "sched_core.h"
#include "wait_pid.h"
#include "signal.h"
#include "ptrace.h"
#include "../time/clock.h"
#include "../fs/epoll.h"
#include "../fs/eventfd.h"
#include "../fs/unix_socket.h"
#include "../mm/usercopy.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
#define TASK (*nas_current_task)
#define signal_actions TASK.sighand->actions
#define blocked_signals TASK.blocked_signals
static void sched_fail(const char *s) __attribute__((noreturn));
static void sched_fail(const char *s) { nas_print("[FAIL] M1 "); nas_print(s); nas_print("\n"); nas_arch_halt(); }
/* The leader is the stable Linux process identity, even after its
 * SYS_exit while other threads are alive. Do not allow wait4 to reap its
 * root or descriptor table until the LAST live member has terminated. */
static bool group_has_live_workers(const struct nas_task *leader) {
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        const struct nas_task *peer=nas_task_at(slot);
        if(peer!=leader && peer->tgid==leader->tgid && peer->is_thread &&
           peer->state!=NAS_TASK_FREE && peer->state!=NAS_TASK_ZOMBIE)
            return true;
    }
    return false;
}
void nas_sched_child_exit(uint32_t code, uint32_t signum) {
    struct nas_task *child = nas_current_task;
    if (!child->ppid || !nas_task_by_pid(child->ppid))
        sched_fail("child termination without living parent");
    nas_futex_wait_cancel(child);
    nas_process_robust_exit(child);
    nas_process_clear_tid_on_exit();
    if (child->is_thread) {
        /* SYS_exit is thread-local. Preserve CLONE_FILES/VM while siblings
         * run; release this task slot only after switching user frames. */
        child->state=NAS_TASK_ZOMBIE;
        child->resume_valid=false;
        nas_task_wait_reset(child);
        struct nas_task *leader=child->group_leader;
        if(leader->group_exit_pending && !group_has_live_workers(leader)) {
            /* Leader finished earlier: group becomes waitable NOW, only
             * after the last worker has cleared its TID and stopped. */
            struct nas_task *last=nas_current_task;
            nas_current_task=leader;
            nas_arch_restore_cpu(leader);
            nas_sched_child_exit(leader->exit_code,leader->termination_signal);
            nas_current_task=last;
            nas_arch_restore_cpu(last);
            nas_print("[THREAD GROUP] last worker finalized tgid=");
            nas_dec(leader->tgid);nas_print("\n");
        }
        return;
    }
    child->exit_code=code&255U;
    child->termination_signal=signum;
    child->state=NAS_TASK_ZOMBIE;
    child->resume_valid=false;
    nas_task_wait_reset(child);
    if(group_has_live_workers(child)) {
        child->group_exit_pending=true;
        nas_print("[THREAD GROUP] leader exit deferred tgid=");
        nas_dec(child->tgid);nas_print("\n");
        return;
    }
    child->group_exit_pending=false;
    if (child->vfork_shared_vm) {
        nas_process_vfork_release();
        /* The child has a separate MM reference; final put leaves
         * the parent-owned shared address space alive. */
    }
    child->exit_code = code & 255U;
    child->termination_signal = signum;
    child->state = NAS_TASK_ZOMBIE;
    child->stop_report_pending = false;
    child->continue_report_pending = false;
    child->stopped_from = NAS_TASK_FREE;
    nas_task_wait_reset(child);
    /* Reparent *both* living and already-zombie descendants to init.
     * Task identity (PID) and physical page ownership remain unchanged.
     * PID 1 owns future wait4 and is responsible for reaping orphans. */
    nas_ptrace_tracer_exit(child->pid);
    nas_task_adopt_orphans(child->pid);
    for (unsigned fd = 0; fd < NAS_FD_LIMIT; fd++)
        if (TASK.fds->entries[fd].kind != FD_CLOSED) nas_fs_close_fd(fd);
    /* All inherited jobserver pipe ends are closed before SIGCHLD is
     * observed. Wake a caught parent signal, never fake a ready pipe. */
    nas_signal_child_exit(child);
    if (signum) {
        if (signum == NAS_SIGPIPE) {
            /* Keep the observable SIGPIPE trace consumed by immutable
             * original BusyBox pipe-lifecycle regression diagnostics. */
            nas_print("[SIGNAL] default SIGPIPE terminated pid=");
        } else {
            nas_print("[SIGNAL] default signal="); nas_dec(signum);
            nas_print(" terminated pid=");
        }
        nas_dec(child->pid);
        nas_print("\n");
    } else {
        nas_print("[PROC] child exited pid="); nas_dec(child->pid);
        nas_print(" status="); nas_dec(child->exit_code);
        nas_print("; parent pid="); nas_dec(child->ppid);
        nas_print("\n");
    }
}
/* Linux exit_group differs from exit: all threads (READY, BLOCKED,
 * STOPPED, and already exited) are stopped, and shared FDs / address space
 * are cleaned up ONCE by their stable process leader. All members share
 * the active user root. Never reclaim the root before parent wait4. */
void nas_sched_thread_group_exit(uint32_t code,uint32_t signum) {
    struct nas_task *caller=nas_current_task;
    struct nas_task *leader=caller->group_leader;
    if(!leader || leader->tgid!=caller->tgid || leader->is_thread)
        sched_fail("exit_group invalid thread group owner");
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        struct nas_task *peer=nas_task_at(slot);
        if(peer==caller || peer==leader || peer->tgid!=leader->tgid ||
           peer->state==NAS_TASK_FREE)continue;
        if(!peer->is_thread) sched_fail("exit_group crossed process boundary");
        nas_futex_wait_cancel(peer);
        nas_process_robust_exit(peer);
        nas_process_clear_tid_for_exit(peer);
        peer->state=NAS_TASK_ZOMBIE;
        peer->resume_valid=false;
        nas_task_wait_reset(peer);
        nas_task_release(peer);
    }
    if(caller!=leader) {
        nas_process_robust_exit(caller);
        nas_process_clear_tid_on_exit();
        caller->state=NAS_TASK_ZOMBIE;
        caller->resume_valid=false;
        nas_task_wait_reset(caller);
        /* Temporarily take ownership of the common process FD cleanup
         * as the group leader. Real CPU root/TLS are restored to caller
         * before unwinding its syscall frame. */
        nas_current_task=leader;
        nas_arch_restore_cpu(leader);
        nas_sched_child_exit(code,signum);
        nas_current_task=caller;
        nas_arch_restore_cpu(caller);
    } else nas_sched_child_exit(code,signum);
    nas_print("[THREAD GROUP] exit_group tgid=");
    nas_dec(leader->tgid);nas_print(" code=");nas_dec(code&255U);
    nas_print("\n");
}

/* A dedicated scheduler continuation is independent of all user kernel stacks.
 * A blocked syscall keeps its C call chain alive and resumes in its own MM.
 * Fresh fork/ptrace Tasks use per-slot bootstrap stacks and native user
 * register images: we must not copy another Task's live trap return frame. */
#define NAS_SCHED_STACK_BYTES 32768U
#define NAS_TASK_BOOTSTRAP_BYTES 8192U
static _Alignas(16) uint8_t scheduler_stack[NAS_SCHED_STACK_BYTES];
static _Alignas(16) uint8_t task_bootstrap[NAS_TASK_LIMIT][NAS_TASK_BOOTSTRAP_BYTES];
static struct nas_kernel_context scheduler_cpu;
static bool scheduler_ready;
static unsigned native_cursor;
static void native_dispatcher(void);
static void native_ensure_scheduler(void) {
    if(scheduler_ready)return;
    if(!nas_kernel_context_prepare(&scheduler_cpu,scheduler_stack,
                                   sizeof(scheduler_stack),native_dispatcher))
        sched_fail("native scheduler context initialization");
    scheduler_ready=true;
}
static void native_park(struct nas_task *self) {
    if(!self || self!=nas_current_task)
        sched_fail("native park without current Task");
    native_ensure_scheduler();
    nas_arch_pin_live_trap(self);
    nas_arch_kernel_swap(&self->kernel_exec.cpu,&scheduler_cpu);
    if(nas_current_task!=self)
        sched_fail("native continuation restored foreign Task");
    /* The resumed C frame now owns the live stack, not a parked frame. */
    self->kernel_exec.cpu.saved_sp=0;
}
static void native_first_user(void) {
    nas_arch_resume_saved_user();
}
static void native_dispatcher(void) {
    for(;;) {
        nas_time_timers_tick();
        nas_sched_service_kernel();
        nas_tty_pump();
        uint64_t sid=0,pgid=0;
        unsigned sig=nas_tty_take_signal(&sid,&pgid);
        if(sig && !nas_signal_tty_event(sid,pgid,sig))
            nas_tty_signal_unsupported();
        struct nas_sched_scan scan;
        nas_sched_scan_init(&scan,0,native_cursor,NAS_TASK_LIMIT);
        unsigned slot=0;
        struct nas_task *next=nas_sched_scan_next(&scan,&slot);
        if(next) {
            if(!next->resume_valid ||
               (next->state!=NAS_TASK_READY &&
                (next->state!=NAS_TASK_BLOCKED ||
                 !nas_task_wait_ready(next))))
                sched_fail("invalid native schedule candidate");
            if(next->state==NAS_TASK_BLOCKED &&
               !next->kernel_exec.cpu.saved_sp)
                sched_fail("blocked Task lost native C continuation");
            next->state=NAS_TASK_RUNNING;
            next->resume_valid=false;
            nas_current_task=next;
            native_cursor=(slot+1U)%NAS_TASK_LIMIT;
            nas_arch_bind_live_trap(next->kernel_exec.cpu.saved_sp ? next : 0);
            nas_arch_restore_cpu(next);
            nas_task_cpu_account_switch(next);
            if(!next->kernel_exec.cpu.saved_sp) {
                if(!nas_kernel_context_prepare(&next->kernel_exec.cpu,
                    task_bootstrap[slot],NAS_TASK_BOOTSTRAP_BYTES,
                    native_first_user))
                    sched_fail("native Task bootstrap allocation");
            }
            nas_arch_kernel_swap(&scheduler_cpu,&next->kernel_exec.cpu);
            continue;
        }
        /* The IRQ-safe idle sleeping transition requires a separate
         * interrupt ownership change; never WFI/HLT with IRQs masked. */
        nas_task_cpu_account_switch(0);
        bool external=nas_time_timers_armed() ||
            nas_kthread_deadlines_armed() || nas_kthread_runnable();
        for(unsigned i=0;i<NAS_TASK_LIMIT && !external;i++)
            external=nas_task_wait_needs_idle_poll(nas_task_at(i));
        if(!external)sched_fail("native scheduler deadlock");
    }
}
uint64_t nas_task_schedule(uint64_t *frame) {
    struct nas_task *self=nas_current_task;
    if(!self)sched_fail("native yield without Task");
    if(self->state==NAS_TASK_RUNNING) {
        nas_arch_save_user_frame(self,frame);
        nas_arch_save_fpu(self);
        self->resume_valid=true;
        self->state=NAS_TASK_READY;
    }
    if(frame) self->native_trap_frame=(uint64_t)(uintptr_t)frame;
    native_park(self);
    /* Ptrace may have modified the parked Task's register snapshot. */
    nas_arch_load_user_frame(self,frame);
    return nas_arch_saved_return(self);
}
bool nas_sched_preempt_select(void) {
    struct nas_task *self=nas_current_task;
    if(!self || self->state!=NAS_TASK_RUNNING)return false;
    self->resume_valid=true;
    self->state=NAS_TASK_READY;
    native_park(self);
    return false; /* original IRQ frame resumes on its own Task stack */
}
uint64_t nas_task_block(uint64_t *frame,enum nas_syscall_op nr,
                        uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    struct nas_task *self=nas_current_task;
    nas_arch_save_user_frame(self,frame);
    nas_arch_save_fpu(self);
    self->resume_valid=true;
    self->wait.blocked_op=nr;
    self->wait.pending_valid=true;
    self->wait.blocked_args[0]=a;
    self->wait.blocked_args[1]=b;
    self->wait.blocked_args[2]=c;
    self->wait.blocked_args[3]=d;
    self->state=NAS_TASK_BLOCKED;
    /* This reports a genuine park, never a synthetic blocking result.
     * Keep the immutable original BusyBox large-pipe gate intact. */
    static unsigned native_block_reports;
    if(native_block_reports++<48U) {
        nas_print("[SCHED] blocked pid=");nas_dec(self->pid);
        nas_print(" syscall=");nas_dec(nr);nas_print("\n");
    }
    if(frame)self->native_trap_frame=(uint64_t)(uintptr_t)frame;
    for(;;) {
        native_park(self);
        nas_arch_load_user_frame(self,frame);
        uint64_t result=nas_arch_saved_return(self);
        enum nas_blocked_completion outcome=nas_blocked_complete(self,&result);
        if(outcome==NAS_BLOCKED_COMPLETE)return result;
        if(outcome==NAS_BLOCKED_TERMINATED) {
            native_park(self);
            sched_fail("terminated Task resumed native continuation");
        }
        self->resume_valid=true;
        if(outcome==NAS_BLOCKED_RETRY)self->state=NAS_TASK_BLOCKED;
    }
}
