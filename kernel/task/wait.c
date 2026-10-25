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
#include "wait_pid.h"
#include "signal.h"
#include "../time/clock.h"
#include "../fs/epoll.h"
#include "../fs/eventfd.h"
#include "../fs/unix_socket.h"
#include "wait.h"

/* Inspect only kernel-owned slots; replay only after selected task's VM
 * has been restored. No foreign user pointer read during readiness scan. */
static bool task_io_ready(const struct nas_task *task) {
    uint64_t fd = task->wait.blocked_args[0];
    uint8_t kind = nas_task_fd_kind(task, fd);
    unsigned slot = nas_task_fd_slot(task, fd);
    if (slot >= NAS_OPEN_FILE_LIMIT) return true; /* replay to return EBADF */
    if (task->wait.blocked_op == NAS_SC_read ||
        task->wait.blocked_op == NAS_SC_readv) {
        if(kind == FD_PIPE_R || kind == FD_SOCKET)
            return nas_unix_socket_listening(slot) ?
                   nas_unix_socket_readable(slot) : nas_pipe_read_ready(slot);
        if(kind == FD_EVENTFD)return nas_eventfd_ready(slot,false);
        if(kind == FD_PTY_MASTER || kind == FD_PTY_SLAVE)
            return nas_pty_read_ready(slot);
        if(kind == FD_TTY || kind == FD_UART)
            return nas_tty_read_ready((struct nas_task *)task,task->wait.blocked_args[2]);
        return true; /* replay and return actual fd error */
    }
    if (task->wait.blocked_op == NAS_SC_write ||
        task->wait.blocked_op == NAS_SC_writev)
        return kind == FD_EVENTFD ? nas_eventfd_ready(slot,true) :
               (kind == FD_PTY_MASTER || kind == FD_PTY_SLAVE) ?
               nas_pty_write_ready(slot) :
               (kind != FD_PIPE_W && kind != FD_SOCKET) ||
               nas_pipe_write_ready(slot);
    return true;
}
static bool task_wait_ready(const struct nas_task *task) {
    uint64_t desired = task->wait.blocked_args[0];
    for (unsigned slot = 0; slot < NAS_TASK_LIMIT; slot++) {
        struct nas_task *child = nas_task_child_at(task->pid, slot);
        if (!child) continue;
        if (!nas_wait_pid_matches(desired,child->pid,
                                  task->pgid,child->pgid)) continue;
        if (nas_process_waitable(child,task->wait.blocked_args[2])) return true;
    }
    return false;
}
bool nas_task_wait_ready(const struct nas_task *task) {
    if (task->state != NAS_TASK_BLOCKED || !task->resume_valid ||
        !task->wait.pending_valid) return false;
    /* A pending caught signal interrupts an outstanding syscall. Signal
     * frame creation is deferred until the target VM and user frame are
     * selected, not while inspecting an unrelated blocked process. */
    if (nas_signal_user_pending(task)) return true;
    /* A kernel-owned monotonic deadline can wake a task independently of
     * pipe/TTY events. No foreign userspace pointer is touched here. */
    if (task->wait.deadline_active &&
        nas_time_deadline_expired(nas_arch_monotonic_ns(),task->wait.deadline_ns))
        return true;
    if (task->wait.blocked_op == NAS_SC_nanosleep ||
        task->wait.blocked_op == NAS_SC_clock_nanosleep) return false;
    if (task->wait.blocked_op == NAS_SC_futex) return task->wait.futex_woken;
    if (task->wait.blocked_op == NAS_SC_wait4) return task_wait_ready(task);
    if(task->wait.blocked_op==NAS_SC_epoll_wait ||
       task->wait.blocked_op==NAS_SC_epoll_pwait)
        return nas_epoll_task_ready(task);
    if (task->wait.blocked_op == NAS_SC_poll ||
        (task->wait.blocked_op == NAS_SC_ppoll ||
         (task->wait.blocked_op == NAS_SC_pselect6 || task->wait.blocked_op == NAS_SC_select)))
        return nas_fs_poll_ready(task);
    if (task->wait.blocked_op == NAS_SC_read ||
        task->wait.blocked_op == NAS_SC_readv ||
        task->wait.blocked_op == NAS_SC_write ||
        task->wait.blocked_op == NAS_SC_writev)
        return task_io_ready(task);
    return false;
}

void nas_task_wait_reset(struct nas_task *task) {
    if(task)task->wait=(struct nas_task_wait){0};
}
