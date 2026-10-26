#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../syscall/ops.h"
#include "../task.h"
#include "../fd.h"
#include "../arch/arch.h"
#include "../panic.h"
#include "../fs/io.h"
#include "../drivers/tty.h"
#include "../mm/usercopy.h"
#include "../fs/epoll.h"
#include "process.h"
#include "sched.h"
#include "signal.h"
#include "ptrace.h"
#include "futex_wait.h"
#include "wait.h"
#include "blocked_completion.h"
#include "../time/clock.h"
#define TASK (*nas_current_task)
#define signal_actions TASK.sighand->actions
#define blocked_signals TASK.blocked_signals
/* Native Task continuation: invoke the originating provider after wake,
 * rather than decoding and replaying the syscall in the scheduler. Only
 * Linux EINTR's optional sleep-remainder copy is shared ABI policy. */
static uint64_t task_resume_syscall(struct nas_task *task) {
    const enum nas_syscall_op nr=(enum nas_syscall_op)task->wait.blocked_op;
    const uint64_t *a=task->wait.blocked_args;
    if(nas_signal_user_pending(task)) {
        if(nr==NAS_SC_nanosleep || nr==NAS_SC_clock_nanosleep) {
            uint64_t rem=nr==NAS_SC_nanosleep ? a[1] :
                ((a[1]&1U) ? 0 : a[3]);
            if(rem) {
                uint64_t now=nas_arch_monotonic_ns();
                uint64_t left=task->wait.deadline_ns>now ?
                              task->wait.deadline_ns-now : 0;
                uint64_t ts[2]={left/NAS_NSEC_PER_SEC,
                                left%NAS_NSEC_PER_SEC};
                if(!nas_user_write_bytes(rem,(const uint8_t *)ts,sizeof(ts)))
                    return (uint64_t)-EFAULT;
            }
        }
        return (uint64_t)-EINTR;
    }
    if(!task->wait.finish)
        nas_fail("native blocked continuation lost completion provider");
    return task->wait.finish(task);
}

/* Broken pipe generates SIGPIPE. POSIX/Linux default action terminates the
 * writer; returning EPIPE alone is insufficient (BusyBox yes retries forever).
 * Blocked or ignored SIGPIPE returns EPIPE to userspace as Linux does.
 * Caught/blocked SIGPIPE queues are handled by the signal producer after
 * the default-termination fast path; a custom handler runs on user return.
 */
bool nas_sched_sigpipe_default(uint64_t fd) {
    return TASK.ppid && fd < NAS_FD_LIMIT &&
        (TASK.fds->entries[fd].kind == FD_PIPE_W || TASK.fds->entries[fd].kind == FD_SOCKET) &&
        signal_actions[NAS_SIGPIPE][0] == 0 &&
        !(blocked_signals & (1ULL << (NAS_SIGPIPE - 1)));
}

/* The resumed kernel C frame owns syscall completion and retry; this
 * adapter retains common signal and ptrace outcome publication.
 * State changes are published only after actual syscall completion. */
enum nas_blocked_completion nas_blocked_complete(struct nas_task *task,
                                                  uint64_t *result) {
    if(!task || !result || task!=nas_current_task)
        nas_fail("M94 blocked completion requires selected Task");
    if(!task->wait.pending_valid)return NAS_BLOCKED_COMPLETE;
    const enum nas_syscall_op nr=(enum nas_syscall_op)task->wait.blocked_op;
    const uint64_t fd=task->wait.blocked_args[0];
    const uint64_t rc=task_resume_syscall(task);
    const bool writing=nr==NAS_SC_write || nr==NAS_SC_writev;
    if((int64_t)rc==-EPIPE && writing) {
        if(nas_sched_sigpipe_default(fd)) {
            nas_sched_child_exit(0,NAS_SIGPIPE);
            return NAS_BLOCKED_TERMINATED;
        }
        nas_signal_broken_pipe(fd);
    }
    /* Preserve internal retry semantics when another task consumes the
     * readiness between poll and execution. Never leak -EOPNOTSUPP from
     * a blocking pipe/socket/eventfd as a successful syscall completion. */
    const uint8_t kind=nas_task_fd_kind(task,fd);
    const bool tty_again=(nr==NAS_SC_read || nr==NAS_SC_readv) &&
        (kind==FD_TTY || kind==FD_UART) &&
        (int64_t)rc==-EAGAIN && nas_tty_read_waitable();
    const bool io_again=(int64_t)rc==-EOPNOTSUPP &&
        (kind==FD_PIPE_R || kind==FD_PIPE_W ||
         kind==FD_SOCKET || kind==FD_EVENTFD);
    const bool poll_again=(int64_t)rc==-EAGAIN &&
        (nr==NAS_SC_poll || nr==NAS_SC_ppoll ||
         nr==NAS_SC_pselect6 || nr==NAS_SC_select ||
         nr==NAS_SC_epoll_wait || nr==NAS_SC_epoll_pwait);
    if(tty_again || io_again || poll_again) {
        task->state=NAS_TASK_BLOCKED;
        return NAS_BLOCKED_RETRY;
    }
    if(nr==NAS_SC_ppoll || nr==NAS_SC_pselect6 ||
       nr==NAS_SC_epoll_pwait || nr==NAS_SC_rt_sigsuspend)
        nas_signal_ppoll_restore(task,(int64_t)rc==-EINTR);
    nas_futex_wait_cancel(task);
    nas_task_wait_reset(task);
    nas_arch_set_saved_return(task,rc);
    *result=rc;
    return NAS_BLOCKED_COMPLETE;
}
