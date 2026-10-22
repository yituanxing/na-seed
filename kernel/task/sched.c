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
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
#define TASK (*nas_current_task)
#define descriptors TASK.fds.descriptors
#define signal_actions TASK.signal_actions
#define blocked_signals TASK.blocked_signals
static void sched_fail(const char *s) __attribute__((noreturn));
static void sched_fail(const char *s) { nas_print("[FAIL] M1 "); nas_print(s); nas_print("\n"); nas_arch_halt(); }
/* M6.4 syscall-boundary cooperative scheduler. All live syscalls unwind on
 * the same kernel stack. Parked syscalls are re-issued ONLY after a wake
 * condition is true and the task's own CR3 and FS state are restored.
 * Thus a switched task always receives the real read/write/wait4 result;
 * a fabricated success or USER RIP rewind would corrupt Linux userspace.
 */
static bool task_io_ready(const struct nas_task *task) {
    uint64_t fd = task->blocked_args[0];
    uint8_t kind = nas_task_fd_kind(task, fd);
    unsigned slot = nas_task_fd_slot(task, fd);
    if (slot >= NAS_FD_LIMIT) return true; /* replay to return EBADF */
    if (task->blocked_op == NAS_SC_read) {
        if(kind == FD_PIPE_R)return nas_pipe_read_ready(slot);
        if(kind == FD_TTY || kind == FD_UART)
            return nas_tty_read_ready(task->blocked_args[2]);
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
    if (task->blocked_op == NAS_SC_wait4) return task_wait_ready(task);
    if (task->blocked_op == NAS_SC_poll ||
        task->blocked_op == NAS_SC_ppoll)
        return nas_fs_poll_ready(task);
    if (task->blocked_op == NAS_SC_read ||
        task->blocked_op == NAS_SC_write ||
        task->blocked_op == NAS_SC_writev)
        return task_io_ready(task);
    return false;
}
static uint64_t task_replay(struct nas_task *task) {
    enum nas_syscall_op nr = (enum nas_syscall_op)task->blocked_op;
    const uint64_t *a = task->blocked_args;
    if (nr == NAS_SC_read) return nas_fs_fd_read(a[0], a[1], a[2]);
    if (nr == NAS_SC_write) return nas_fs_fd_write(a[0], a[1], a[2]);
    if (nr == NAS_SC_writev) return nas_fs_fd_writev(a[0], a[1], a[2]);
    if (nr == NAS_SC_wait4) return nas_process_wait4(a[0], a[1], a[2], a[3]);
    if (nr == NAS_SC_poll) return nas_fs_poll(a[0],a[1],a[2]);
    if (nr == NAS_SC_ppoll) return nas_fs_ppoll(a[0],a[1],a[2],a[3]);
    sched_fail("scheduler unknown blocked syscall");
}

/* Broken pipe generates SIGPIPE. POSIX/Linux default action terminates the
 * writer; returning EPIPE alone is insufficient (BusyBox yes retries forever).
 * Blocked or ignored SIGPIPE returns EPIPE to userspace as Linux does.
 * A custom userspace handler requires signal-frame delivery (not yet here).
 */
bool nas_sched_sigpipe_default(uint64_t fd) {
    return TASK.ppid && fd < NAS_FD_LIMIT &&
        descriptors[fd] == FD_PIPE_W &&
        signal_actions[NAS_SIGPIPE][0] == 0 &&
        !(blocked_signals & (1ULL << (NAS_SIGPIPE - 1)));
}
void nas_sched_child_exit(uint32_t code, uint32_t signum) {
    struct nas_task *child = nas_current_task;
    if (!child->ppid || !nas_task_by_pid(child->ppid))
        sched_fail("child termination without living parent");
    nas_process_clear_tid_on_exit();
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
static unsigned schedule_count;
uint64_t nas_task_schedule(uint64_t *frame) {
retry_schedule: ; /* label must precede a statement in C11 */
    struct nas_task *previous = nas_current_task;
    uint64_t previous_pid = previous->pid; /* zombie may be reaped on replay */
    /* A READY task always gets one turn before re-evaluating blocked tasks.
     * This allows ash to construct consumers after a producer fills its pipe.
     */
    nas_tty_pump(); /* physical RX progress even while other tasks run */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned slot = 0; slot < NAS_TASK_LIMIT; ++slot) {
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
                /* Recheck after another task consumed a ready pipe/TTY.
                 * EAGAIN means physical tty RX is genuinely not ready.
                 * EOPNOTSUPP remains the pipe's internal block sentinel,
                 * but unsupported tty termios/ISIG must reach userspace. */
                uint8_t replay_kind=nas_task_fd_kind(next,next->blocked_args[0]);
                bool tty_again=next->blocked_op==NAS_SC_read &&
                    (replay_kind==FD_TTY || replay_kind==FD_UART) &&
                    (int64_t)result==-EAGAIN && nas_tty_read_waitable();
                bool pipe_again=(int64_t)result==-EOPNOTSUPP &&
                    (replay_kind==FD_PIPE_R || replay_kind==FD_PIPE_W);
                bool poll_again=(next->blocked_op==NAS_SC_poll ||
                                 next->blocked_op==NAS_SC_ppoll) &&
                                 (int64_t)result==-EAGAIN;
                if (tty_again || pipe_again || poll_again) {
                    next->state = NAS_TASK_BLOCKED;
                    nas_current_task = previous;
                    nas_arch_restore_cpu(previous);
                    continue;
                }
                next->pending_valid = false;
                next->blocked_op = 0;
            }
            next->state = NAS_TASK_RUNNING;
            next->resume_valid = false;
            nas_arch_load_user_frame(next, frame);
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
        if((t->blocked_op==NAS_SC_poll || t->blocked_op==NAS_SC_ppoll) &&
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
