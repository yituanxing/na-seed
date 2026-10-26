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
static unsigned schedule_count;
/* A producer repeatedly woken via futex must not starve later TID slots.
 * Rotate task selection across READY and woken blocked tasks rather than
 * always preferring the first task-table slot. Keep the same saved-frame
 * switching mechanism and the READY-before-BLOCKED priority policy. */
static unsigned schedule_cursor;

/* Both syscall-boundary switches and timer preemption use the same
 * candidate activation boundary. A blocked syscall must execute only after
 * its own MM/TLS/FPU have been restored; on retry/stop/exit, restore the
 * caller before visiting another Task. Neither path may transfer a live
 * kernel C continuation while trap entry still owns the current stack. */
static enum nas_blocked_completion sched_prepare_candidate(
    struct nas_task *from,struct nas_task *next,uint64_t *result) {
    nas_current_task=next;
    nas_arch_restore_cpu(next);
    *result=nas_arch_saved_return(next);
    enum nas_blocked_completion outcome=nas_blocked_complete(next,result);
    if(outcome!=NAS_BLOCKED_COMPLETE) {
        nas_current_task=from;
        nas_arch_restore_cpu(from);
    }
    return outcome;
}

/* Cooperative scheduling and timer IRQs must agree on eligible Tasks.
 * IRQ callers have already snapshotted their interrupted user registers.
 * We never save or transfer C stack continuations. Return false if no
 * alternative Task can actually run; the original interrupted context
 * remains valid and its CPU/MMU/FPU are restored after tentative replays.
 */
bool nas_sched_preempt_select(void) {
    nas_time_timers_tick();
    nas_sched_service_kernel();
    struct nas_task *from=nas_current_task;
    if (!from || from->state != NAS_TASK_RUNNING) return false;
    unsigned origin=NAS_TASK_LIMIT;
    for (unsigned i=0;i<NAS_TASK_LIMIT;i++)
        if (nas_task_at(i)==from) { origin=i; break; }
    if (origin==NAS_TASK_LIMIT) sched_fail("timer task not registered");
    struct nas_sched_scan scan;
    nas_sched_scan_init(&scan,from,(origin+1U)%NAS_TASK_LIMIT,
                        NAS_TASK_LIMIT-1U);
    struct nas_task *next;
    while((next=nas_sched_scan_next(&scan,0))!=0) {
        uint64_t result=0;
        if(sched_prepare_candidate(from,next,&result)!=NAS_BLOCKED_COMPLETE)
            continue;
        if(!nas_sched_user_commit(from,next,true))
            sched_fail("timer committed invalid task state");
        nas_task_cpu_account_switch(next);
        return true;
    }
    nas_current_task=from;
    return false;
}

uint64_t nas_task_schedule(uint64_t *frame) {
retry_schedule: ; /* label must precede a statement in C11 */
    nas_time_timers_tick();
    nas_sched_service_kernel();
    struct nas_task *previous = nas_current_task;
    uint64_t previous_pid = previous->pid; /* zombie may be reaped on replay */
    /* A READY task always gets one turn before re-evaluating blocked tasks.
     * This allows ash to construct consumers after a producer fills its pipe.
     */
    nas_tty_pump(); /* physical RX progress even while other tasks run */
    uint64_t tty_sid=0,tty_pgid=0;
    unsigned tty_sig=nas_tty_take_signal(&tty_sid,&tty_pgid);
    if(tty_sig && !nas_signal_tty_event(tty_sid,tty_pgid,tty_sig))
        nas_tty_signal_unsupported();
    struct nas_sched_scan scan;
    nas_sched_scan_init(&scan,previous,schedule_cursor,NAS_TASK_LIMIT);
    unsigned slot=0;
    struct nas_task *next;
    while((next=nas_sched_scan_next(&scan,&slot))!=0) {
        uint64_t result=0;
        enum nas_blocked_completion outcome=
            sched_prepare_candidate(previous,next,&result);
        if(outcome!=NAS_BLOCKED_COMPLETE) {
            if(outcome==NAS_BLOCKED_TERMINATED ||
               outcome==NAS_BLOCKED_PTRACE_STOP)
                goto retry_schedule;
            continue;
        }
        if(!nas_sched_user_commit(previous,next,false))
            sched_fail("syscall committed invalid task state");
        nas_task_cpu_account_switch(next);
        schedule_cursor=(slot+1U)%NAS_TASK_LIMIT;
        nas_arch_load_user_frame(next,frame);
        /* No active C continuation can keep a stale last-exit thread slot. */
        if(previous!=next && previous->is_thread &&
           previous->state==NAS_TASK_ZOMBIE)
            nas_task_release(previous);
        if(schedule_count++<48) {
            nas_print("[SCHED] from=");nas_dec(previous_pid);
            nas_print(" to=");nas_dec(next->pid);nas_print("\n");
        }
        return result;
    }
    /* No user Task occupies the CPU while the no-runnable-task dispatcher
     * polls TTY/device readiness. Meter this as idle until a Task resumes. */
    nas_task_cpu_account_switch(0);
    /* Unlike pipe deadlock, a TTY read can be completed by future REAL
     * external UART input even when the last task is blocked. Keep scanning
     * actual device RX until a line or VMIN threshold arrives. This idle
     * dispatcher polls hardware; IRQ-driven low-power waiting is not yet
     * implemented and must not be described as interrupt-driven. */
    /* IRQ-driven idle is not implemented. Keep the polling fallback in one
     * place, but delegate device/syscall-specific progress policy to Wait.
     * This preserves live TTY, epoll, timer and worker wakeups without
     * embedding syscall decoding in the CPU scheduler. */
    bool externally_wakeable=nas_time_timers_armed() ||
        nas_kthread_deadlines_armed() || nas_kthread_runnable();
    for(unsigned slot=0;slot<NAS_TASK_LIMIT && !externally_wakeable;slot++)
        externally_wakeable=nas_task_wait_needs_idle_poll(nas_task_at(slot));
    if(externally_wakeable) {
        __asm__ volatile ("" ::: "memory");
        goto retry_schedule;
    }
    /* Fatal deadlock diagnostic: dump who holds each pipe endpoint. */
    for (unsigned i = 0; i < NAS_PIPE_COUNT; i++) {
        unsigned count = 0;
        bool readers = false, writers = false;
        if (!nas_pipe_snapshot(i, &count, &readers, &writers)) continue;
        nas_print("[DEADLOCK] pipe="); nas_dec(i);
        nas_print(" bytes="); nas_dec(count);
        nas_print(" readers="); nas_dec(readers);
        nas_print(" writers="); nas_dec(writers);
        nas_print("\n");
    }
    for (unsigned i = 0; i < NAS_TASK_LIMIT; i++) {
        struct nas_task *task = nas_task_at(i);
        if (task->state == NAS_TASK_FREE) continue;
        nas_print("[DEADLOCK] pid="); nas_dec(task->pid);
        nas_print(" state="); nas_dec(task->state);
        nas_print(" blocked_op="); nas_dec(task->wait.blocked_op);
        nas_print(" trace_parent=");nas_dec(task->ptrace_tracer_pid);
        nas_print(" ptrace_phase=");nas_dec(task->ptrace_phase);
        nas_print(" ptrace_sig=");nas_dec(task->stop_signal);
        nas_print(" report=");nas_dec(task->stop_report_pending);
        nas_print(" wait_target=");nas_dec(task->wait.blocked_args[0]);
        nas_print(" wait_flags=");nas_hex(task->wait.blocked_args[2]);
        nas_print(" fd="); nas_dec(task->wait.blocked_args[0]);
        nas_print("\n");
        if(task->wait.blocked_op==NAS_SC_futex) {
            nas_print("[DEADLOCK] futex tid=");nas_dec(task->pid);
            nas_print(" tgid=");nas_dec(task->tgid);
            nas_print(" addr=");nas_hex(task->wait.futex_addr);
            nas_print(" expected=");nas_hex(task->wait.blocked_args[2]);
            nas_print(" mask=");nas_hex(task->wait.futex_mask);
            nas_print(" signaled=");nas_dec(task->wait.futex_woken);
            nas_print("\n");
        }
        for (unsigned fd = 0; fd < NAS_FD_LIMIT; fd++) {
            uint8_t kind = nas_task_fd_kind(task, fd);
            if (kind != FD_PIPE_R && kind != FD_PIPE_W &&
                kind != FD_SOCKET) continue;
            unsigned slot = nas_task_fd_slot(task, fd);
            nas_print("[DEADLOCK] holder pid="); nas_dec(task->pid);
            nas_print(" fd="); nas_dec(fd);
            nas_print(" kind="); nas_dec(kind);
            nas_print(" slot="); nas_dec(slot);
            nas_print("\n");
        }
    }
    sched_fail("scheduler deadlock: all live tasks blocked; no ready process");
}
uint64_t nas_task_block(uint64_t *frame, enum nas_syscall_op nr,
                           uint64_t a, uint64_t b, uint64_t c,
                           uint64_t d) {
    struct nas_task *task = nas_current_task;
    nas_arch_save_user_frame(task, frame);
    task->resume_valid = true;
    task->wait.blocked_op = nr;
    task->wait.pending_valid = true;
    task->ptrace_blocked_exit_pending=task->ptrace_trace_syscalls && task->ptrace_tracer_pid;
    task->wait.blocked_args[0] = a;
    task->wait.blocked_args[1] = b;
    task->wait.blocked_args[2] = c;
    task->wait.blocked_args[3] = d;
    task->state = NAS_TASK_BLOCKED;
    nas_arch_save_fpu(task);
    if (schedule_count < 48) {
        nas_print("[SCHED] blocked pid="); nas_dec(task->pid);
        nas_print(" syscall="); nas_dec(nr); nas_print("\n");
    }
    return nas_task_schedule(frame);
}
