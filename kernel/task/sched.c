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
#include "process.h"
#include "sched.h"
#include "wait_pid.h"
#include "signal.h"
#include "../time/clock.h"
#include "../fs/epoll.h"
#include "../mm/usercopy.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
#define TASK (*nas_current_task)
#define descriptors TASK.fds->descriptors
#define signal_actions TASK.vm_owner->signal_actions
#define blocked_signals TASK.blocked_signals
static void sched_fail(const char *s) __attribute__((noreturn));
static void sched_fail(const char *s) { nas_print("[FAIL] M1 "); nas_print(s); nas_print("\n"); nas_arch_halt(); }
/* Cooperative syscall-boundary scheduler. Per-task entry stacks are
 * distinct, but kernel C continuations are still unwound before switching.
 * Parked syscalls are re-issued ONLY after their wake condition and after
 * the task's own CR3/TLS/FPU state are restored.
 * Thus a switched task always receives the real read/write/wait4 result;
 * a fabricated success or USER RIP rewind would corrupt Linux userspace.
 */
static bool task_io_ready(const struct nas_task *task) {
    uint64_t fd = task->blocked_args[0];
    uint8_t kind = nas_task_fd_kind(task, fd);
    unsigned slot = nas_task_fd_slot(task, fd);
    if (slot >= NAS_FD_LIMIT) return true; /* replay to return EBADF */
    if (task->blocked_op == NAS_SC_read ||
        task->blocked_op == NAS_SC_readv) {
        if(kind == FD_PIPE_R)return nas_pipe_read_ready(slot);
        if(kind == FD_TTY || kind == FD_UART)
            return nas_tty_read_ready((struct nas_task *)task,task->blocked_args[2]);
        return true; /* replay and return actual fd error */
    }
    if (task->blocked_op == NAS_SC_write ||
        task->blocked_op == NAS_SC_writev)
        return kind != FD_PIPE_W || nas_pipe_write_ready(slot);
    return true;
}
static bool task_wait_ready(const struct nas_task *task) {
    uint64_t desired = task->blocked_args[0];
    for (unsigned slot = 0; slot < NAS_TASK_LIMIT; slot++) {
        struct nas_task *child = nas_task_child_at(task->pid, slot);
        if (!child) continue;
        if (!nas_wait_pid_matches(desired,child->pid,
                                  task->pgid,child->pgid)) continue;
        if (nas_process_waitable(child,task->blocked_args[2])) return true;
    }
    return false;
}
static bool task_can_wake(const struct nas_task *task) {
    if (task->state != NAS_TASK_BLOCKED || !task->resume_valid ||
        !task->pending_valid) return false;
    /* A pending caught signal interrupts an outstanding syscall. Signal
     * frame creation is deferred until the target VM and user frame are
     * selected, not while inspecting an unrelated blocked process. */
    if (nas_signal_user_pending(task)) return true;
    /* A kernel-owned monotonic deadline can wake a task independently of
     * pipe/TTY events. No foreign userspace pointer is touched here. */
    if (task->deadline_active &&
        nas_time_deadline_expired(nas_arch_monotonic_ns(),task->deadline_ns))
        return true;
    if (task->blocked_op == NAS_SC_nanosleep ||
        task->blocked_op == NAS_SC_clock_nanosleep) return false;
    if (task->blocked_op == NAS_SC_futex) return task->futex_woken;
    if (task->blocked_op == NAS_SC_wait4) return task_wait_ready(task);
    if(task->blocked_op==NAS_SC_epoll_wait ||
       task->blocked_op==NAS_SC_epoll_pwait)
        return nas_epoll_task_ready(task);
    if (task->blocked_op == NAS_SC_poll ||
        (task->blocked_op == NAS_SC_ppoll ||
         (task->blocked_op == NAS_SC_pselect6 || task->blocked_op == NAS_SC_select)))
        return nas_fs_poll_ready(task);
    if (task->blocked_op == NAS_SC_read ||
        task->blocked_op == NAS_SC_readv ||
        task->blocked_op == NAS_SC_write ||
        task->blocked_op == NAS_SC_writev)
        return task_io_ready(task);
    return false;
}
static uint64_t task_replay(struct nas_task *task) {
    enum nas_syscall_op nr = (enum nas_syscall_op)task->blocked_op;
    const uint64_t *a = task->blocked_args;
    if (nas_signal_user_pending(task)) {
        /* Linux nanosleep and relative clock_nanosleep expose the REMAINING
         * time, not a stale original duration. Replay runs with this exact
         * Task's user page table restored, never a foreign userspace root. */
        if(nr==NAS_SC_nanosleep || nr==NAS_SC_clock_nanosleep) {
            uint64_t rem=nr==NAS_SC_nanosleep ? a[1] :
                ((a[1]&1U) ? 0 : a[3]); /* TIMER_ABSTIME: no remainder */
            if(rem) {
                uint64_t current=nas_arch_monotonic_ns();
                uint64_t left=task->deadline_ns>current ?
                              task->deadline_ns-current : 0;
                uint64_t ts[2]={left/NAS_NSEC_PER_SEC,
                                left%NAS_NSEC_PER_SEC};
                if(!nas_user_write_bytes(rem,(const uint8_t *)ts,sizeof(ts)))
                    return (uint64_t)-EFAULT;
            }
        }
        return (uint64_t)-EINTR;
    }
    /* Sleep input was validated and converted into task-owned absolute
     * deadline at the original syscall boundary; never restart a relative
     * timeout or dereference its userspace timespec during replay. */
    if (nr == NAS_SC_nanosleep || nr == NAS_SC_clock_nanosleep)
        return 0;
    if (nr == NAS_SC_futex) return task->futex_woken ? 0 : (uint64_t)-ETIMEDOUT;
    if (nr == NAS_SC_read) return nas_fs_fd_read(a[0], a[1], a[2]);
    if (nr == NAS_SC_readv) return nas_fs_fd_readv(a[0], a[1], a[2]);
    if (nr == NAS_SC_write) return nas_fs_fd_write(a[0], a[1], a[2]);
    if (nr == NAS_SC_writev) return nas_fs_fd_writev(a[0], a[1], a[2]);
    if (nr == NAS_SC_wait4) return nas_process_wait4(a[0], a[1], a[2], a[3]);
    if(nr==NAS_SC_epoll_wait || nr==NAS_SC_epoll_pwait)
        return nas_epoll_wait(a[0],a[1],a[2],a[3]);
    if (nr == NAS_SC_poll) return nas_fs_poll(a[0],a[1],a[2]);
    if (nr == NAS_SC_ppoll) return nas_fs_ppoll(a[0],a[1],a[2],a[3]);
    if (nr == NAS_SC_pselect6 || nr == NAS_SC_select)
        return nas_fs_pselect6(a[0],a[1],a[2],a[3],false);
    sched_fail("scheduler unknown blocked syscall");
}

/* Broken pipe generates SIGPIPE. POSIX/Linux default action terminates the
 * writer; returning EPIPE alone is insufficient (BusyBox yes retries forever).
 * Blocked or ignored SIGPIPE returns EPIPE to userspace as Linux does.
 * Caught/blocked SIGPIPE queues are handled by the signal producer after
 * the default-termination fast path; a custom handler runs on user return.
 */
bool nas_sched_sigpipe_default(uint64_t fd) {
    return TASK.ppid && fd < NAS_FD_LIMIT &&
        descriptors[fd] == FD_PIPE_W &&
        signal_actions[NAS_SIGPIPE][0] == 0 &&
        !(blocked_signals & (1ULL << (NAS_SIGPIPE - 1)));
}
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
    nas_process_robust_exit(child);
    nas_process_clear_tid_on_exit();
    if (child->is_thread) {
        /* SYS_exit is thread-local. Preserve CLONE_FILES/VM while siblings
         * run; release this task slot only after switching user frames. */
        child->state=NAS_TASK_ZOMBIE;
        child->resume_valid=false;
        child->pending_valid=false;
        child->blocked_op=0;
        child->futex_addr=0;
        struct nas_task *leader=child->vm_owner;
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
    child->pending_valid=false;
    child->blocked_op=0;
    if(group_has_live_workers(child)) {
        child->group_exit_pending=true;
        nas_print("[THREAD GROUP] leader exit deferred tgid=");
        nas_dec(child->tgid);nas_print("\n");
        return;
    }
    child->group_exit_pending=false;
    if (child->vfork_shared_vm) {
        nas_process_vfork_release();
        /* Exit-before-exec must never let wait4 free parent's page table. */
        nas_task_set_root(child, 0);
    }
    child->exit_code = code & 255U;
    child->termination_signal = signum;
    child->state = NAS_TASK_ZOMBIE;
    child->stop_report_pending = false;
    child->continue_report_pending = false;
    child->stopped_from = NAS_TASK_FREE;
    child->pending_valid = false;
    child->blocked_op = 0;
    /* Reparent *both* living and already-zombie descendants to init.
     * Task identity (PID) and physical page ownership remain unchanged.
     * PID 1 owns future wait4 and is responsible for reaping orphans. */
    nas_task_adopt_orphans(child->pid);
    for (unsigned fd = 0; fd < NAS_FD_LIMIT; fd++)
        if (descriptors[fd] != FD_CLOSED) nas_fs_close_fd(fd);
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
    struct nas_task *leader=caller->vm_owner;
    if(!leader || leader->tgid!=caller->tgid || leader->is_thread)
        sched_fail("exit_group invalid thread group owner");
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        struct nas_task *peer=nas_task_at(slot);
        if(peer==caller || peer==leader || peer->tgid!=leader->tgid ||
           peer->state==NAS_TASK_FREE)continue;
        if(!peer->is_thread) sched_fail("exit_group crossed process boundary");
        nas_process_robust_exit(peer);
        nas_process_clear_tid_for_exit(peer);
        peer->state=NAS_TASK_ZOMBIE;
        peer->resume_valid=false;
        peer->pending_valid=false;
        peer->blocked_op=0;
        peer->futex_addr=0;
        nas_task_release(peer);
    }
    if(caller!=leader) {
        nas_process_robust_exit(caller);
        nas_process_clear_tid_on_exit();
        caller->state=NAS_TASK_ZOMBIE;
        caller->resume_valid=false;
        caller->pending_valid=false;
        caller->blocked_op=0;
        caller->futex_addr=0;
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

/* Cooperative scheduling and timer IRQs must agree on eligible Tasks.
 * IRQ callers have already snapshotted their interrupted user registers.
 * We never save or transfer C stack continuations. Return false if no
 * alternative Task can actually run; the original interrupted context
 * remains valid and its CPU/MMU/FPU are restored after tentative replays.
 */
bool nas_sched_preempt_select(void) {
    nas_time_timers_tick();
    struct nas_task *from=nas_current_task;
    if (!from || from->state != NAS_TASK_RUNNING) return false;
    unsigned origin=NAS_TASK_LIMIT;
    for (unsigned i=0;i<NAS_TASK_LIMIT;i++)
        if (nas_task_at(i)==from) { origin=i; break; }
    if (origin==NAS_TASK_LIMIT) sched_fail("timer task not registered");
    for (unsigned pass=0;pass<2;pass++) {
        for (unsigned offset=1;offset<NAS_TASK_LIMIT;offset++) {
            struct nas_task *next=nas_task_at((origin+offset)%NAS_TASK_LIMIT);
            if (!next->resume_valid) continue;
            if (pass==0 && next->state != NAS_TASK_READY) continue;
            if (pass==1 && !task_can_wake(next)) continue;
            nas_current_task=next;
            nas_arch_restore_cpu(next);
            uint64_t result=nas_arch_saved_return(next);
            if (next->pending_valid) {
                result=task_replay(next);
                if ((int64_t)result==-EPIPE &&
                    (next->blocked_op==NAS_SC_write ||
                     next->blocked_op==NAS_SC_writev) &&
                    nas_sched_sigpipe_default(next->blocked_args[0])) {
                    nas_sched_child_exit(0,NAS_SIGPIPE);
                    nas_current_task=from;
                    nas_arch_restore_cpu(from);
                    continue;
                }
                if((int64_t)result==-EPIPE &&
                   (next->blocked_op==NAS_SC_write ||
                    next->blocked_op==NAS_SC_writev))
                    nas_signal_broken_pipe(next->blocked_args[0]);
                uint8_t kind=nas_task_fd_kind(next,next->blocked_args[0]);
                bool tty_again=(next->blocked_op==NAS_SC_read ||
                                next->blocked_op==NAS_SC_readv) &&
                    (kind==FD_TTY || kind==FD_UART) &&
                    (int64_t)result==-EAGAIN && nas_tty_read_waitable();
                bool pipe_again=(int64_t)result==-EOPNOTSUPP &&
                    (kind==FD_PIPE_R || kind==FD_PIPE_W);
                bool poll_again=(int64_t)result==-EAGAIN &&
                    (next->blocked_op==NAS_SC_poll ||
                     next->blocked_op==NAS_SC_ppoll ||
                     next->blocked_op==NAS_SC_pselect6 ||
                     next->blocked_op==NAS_SC_select ||
                     next->blocked_op==NAS_SC_epoll_wait ||
                     next->blocked_op==NAS_SC_epoll_pwait);
                if (tty_again || pipe_again || poll_again) {
                    nas_current_task=from;
                    nas_arch_restore_cpu(from);
                    continue;
                }
                nas_arch_set_saved_return(next,result);
                if(next->blocked_op==NAS_SC_ppoll ||
                   next->blocked_op==NAS_SC_pselect6 ||
                   next->blocked_op==NAS_SC_epoll_pwait)
                    nas_signal_ppoll_restore(next,(int64_t)result==-EINTR);
                next->pending_valid=false;
                next->blocked_op=0;
                next->deadline_active=false;
            }
            from->state=NAS_TASK_READY;
            from->resume_valid=true;
            next->state=NAS_TASK_RUNNING;
            next->resume_valid=false;
            return true;
        }
    }
    nas_current_task=from;
    return false;
}

uint64_t nas_task_schedule(uint64_t *frame) {
retry_schedule: ; /* label must precede a statement in C11 */
    nas_time_timers_tick();
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
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned offset = 0; offset < NAS_TASK_LIMIT; ++offset) {
            unsigned slot = (schedule_cursor + offset) % NAS_TASK_LIMIT;
            struct nas_task *next = nas_task_at(slot);
            /* A blocked current task may wake when it is the LAST task.
             * Never resume a RUNNING task from its own syscall frame. */
            if (!next->resume_valid ||
                (next == previous && next->state == NAS_TASK_RUNNING))
                continue;
            if (pass == 0 && next->state != NAS_TASK_READY) continue;
            if (pass == 1 && !task_can_wake(next)) continue;
            nas_current_task = next;
            nas_arch_restore_cpu(next);
            uint64_t result = nas_arch_saved_return(next);
            if (next->pending_valid) {
                result = task_replay(next);
                if ((int64_t)result == -EPIPE &&
                    (next->blocked_op == NAS_SC_write ||
                     next->blocked_op == NAS_SC_writev) &&
                    nas_sched_sigpipe_default(next->blocked_args[0])) {
                    nas_sched_child_exit(0, NAS_SIGPIPE);
                    goto retry_schedule; /* parent can now observe zombie */
                }
                if((int64_t)result==-EPIPE &&
                   (next->blocked_op==NAS_SC_write ||
                    next->blocked_op==NAS_SC_writev))
                    nas_signal_broken_pipe(next->blocked_args[0]);
                /* Recheck after another task consumed a ready pipe/TTY.
                 * EAGAIN means physical tty RX is genuinely not ready.
                 * EOPNOTSUPP remains the pipe's internal block sentinel,
                 * but unsupported tty termios/ISIG must reach userspace. */
                uint8_t replay_kind=nas_task_fd_kind(next,next->blocked_args[0]);
                bool tty_again=(next->blocked_op==NAS_SC_read ||
                                next->blocked_op==NAS_SC_readv) &&
                    (replay_kind==FD_TTY || replay_kind==FD_UART) &&
                    (int64_t)result==-EAGAIN && nas_tty_read_waitable();
                bool pipe_again=(int64_t)result==-EOPNOTSUPP &&
                    (replay_kind==FD_PIPE_R || replay_kind==FD_PIPE_W);
                bool poll_again=(int64_t)result==-EAGAIN &&
                    (next->blocked_op==NAS_SC_poll ||
                     next->blocked_op==NAS_SC_ppoll ||
                     next->blocked_op==NAS_SC_pselect6 ||
                     next->blocked_op==NAS_SC_select ||
                     next->blocked_op==NAS_SC_epoll_wait ||
                     next->blocked_op==NAS_SC_epoll_pwait);
                if (tty_again || pipe_again || poll_again) {
                    next->state = NAS_TASK_BLOCKED;
                    nas_current_task = previous;
                    nas_arch_restore_cpu(previous);
                    continue;
                }
                if(next->blocked_op==NAS_SC_ppoll ||
                   next->blocked_op==NAS_SC_pselect6 ||
                   next->blocked_op==NAS_SC_epoll_pwait)
                    nas_signal_ppoll_restore(next,(int64_t)result==-EINTR);
                next->pending_valid = false;
                next->blocked_op = 0;
                next->deadline_active = false;
            }
            next->state = NAS_TASK_RUNNING;
            next->resume_valid = false;
            schedule_cursor = (slot + 1) % NAS_TASK_LIMIT;
            nas_arch_load_user_frame(next, frame);
            /* The old thread's kernel stack is no longer active after the
             * syscall frame is transferred. Its TID slot can be recycled. */
            if (previous!=next && previous->is_thread &&
                previous->state==NAS_TASK_ZOMBIE)
                nas_task_release(previous);
            if (schedule_count++ < 48) {
                nas_print("[SCHED] from="); nas_dec(previous_pid);
                nas_print(" to="); nas_dec(next->pid); nas_print("\n");
            }
            return result;
        }
    }
    /* Unlike pipe deadlock, a TTY read can be completed by future REAL
     * external UART input even when the last task is blocked. Keep scanning
     * actual device RX until a line or VMIN threshold arrives. This idle
     * dispatcher polls hardware; IRQ-driven low-power waiting is not yet
     * implemented and must not be described as interrupt-driven. */
    for (unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        const struct nas_task *t=nas_task_at(slot);
        if (t->state != NAS_TASK_BLOCKED || !t->pending_valid)continue;
        if((t->blocked_op==NAS_SC_epoll_wait ||
             t->blocked_op==NAS_SC_epoll_pwait) && t->poll_tty_read) {
            __asm__ volatile ("" ::: "memory");
            goto retry_schedule;
        }
        if((t->blocked_op==NAS_SC_poll ||
             t->blocked_op==NAS_SC_ppoll ||
             (t->blocked_op==NAS_SC_pselect6 || t->blocked_op==NAS_SC_select)) &&
           t->poll_tty_read) {
            __asm__ volatile ("" ::: "memory");
            goto retry_schedule;
        }
        if(t->blocked_op != NAS_SC_read)continue;
        uint8_t kind=nas_task_fd_kind(t,t->blocked_args[0]);
        if ((kind==FD_TTY || kind==FD_UART) && nas_tty_read_waitable()) {
            __asm__ volatile ("" ::: "memory");
            goto retry_schedule;
        }
    }
    /* All runnable Tasks may be asleep or waiting for a finite I/O
     * deadline. Poll the real hardware monotonic counter with IRQs masked;
     * no interrupt is delivered into non-preemptible kernel C frames.
     * This is an intentional bounded-feature busy idle, not tick-based
     * fake advancement or a premature interruptible kernel scheduler. */
    for (unsigned slot=0;slot<NAS_TASK_LIMIT;slot++) {
        const struct nas_task *t=nas_task_at(slot);
        if(t->state==NAS_TASK_BLOCKED && t->pending_valid &&
           t->deadline_active) {
            __asm__ volatile ("" ::: "memory");
            goto retry_schedule;
        }
    }
    if(nas_time_timers_armed()) {
        __asm__ volatile ("" ::: "memory");
        goto retry_schedule;
    }
    /* A cross-process epoll_ctl or physical UART arrival may wake a
     * blocked epoll waiter even if no timer is currently armed. */
    for(unsigned slot=0;slot<NAS_TASK_LIMIT;slot++){
        const struct nas_task *t=nas_task_at(slot);
        if(t->state==NAS_TASK_BLOCKED && t->pending_valid &&
           (t->blocked_op==NAS_SC_epoll_wait ||
            t->blocked_op==NAS_SC_epoll_pwait)) {
            __asm__ volatile ("" ::: "memory");
            goto retry_schedule;
        }
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
        nas_print(" blocked_op="); nas_dec(task->blocked_op);
        nas_print(" fd="); nas_dec(task->blocked_args[0]);
        nas_print("\n");
        if(task->blocked_op==NAS_SC_futex) {
            nas_print("[DEADLOCK] futex tid=");nas_dec(task->pid);
            nas_print(" tgid=");nas_dec(task->tgid);
            nas_print(" addr=");nas_hex(task->futex_addr);
            nas_print(" expected=");nas_hex(task->blocked_args[2]);
            nas_print(" mask=");nas_hex(task->futex_mask);
            nas_print(" signaled=");nas_dec(task->futex_woken);
            nas_print("\n");
        }
        for (unsigned fd = 0; fd < NAS_FD_LIMIT; fd++) {
            uint8_t kind = nas_task_fd_kind(task, fd);
            if (kind != FD_PIPE_R && kind != FD_PIPE_W) continue;
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
    task->blocked_op = nr;
    task->pending_valid = true;
    task->blocked_args[0] = a;
    task->blocked_args[1] = b;
    task->blocked_args[2] = c;
    task->blocked_args[3] = d;
    task->state = NAS_TASK_BLOCKED;
    nas_arch_save_fpu(task);
    if (schedule_count < 48) {
        nas_print("[SCHED] blocked pid="); nas_dec(task->pid);
        nas_print(" syscall="); nas_dec(nr); nas_print("\n");
    }
    return nas_task_schedule(frame);
}
