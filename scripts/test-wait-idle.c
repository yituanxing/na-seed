/* Link the production wait policy, not a reimplementation of its branches.
 * A future native idle transition must still recognize every possible
 * external producer without putting syscall decoding back in sched.c. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task/wait.h"
#include "../kernel/syscall/ops.h"
static bool terminal_waitable=true;
uint8_t nas_task_fd_kind(const struct nas_task *task,uint64_t fd) {
    (void)task;
    return fd==1?FD_TTY:FD_PIPE_R;
}
bool nas_tty_read_waitable(void) {return terminal_waitable;}
int main(void) {
    struct nas_task t={0};
    assert(!nas_task_wait_has_external_wake_source(0));
    t.state=NAS_TASK_READY;t.wait.pending_valid=true;
    assert(!nas_task_wait_has_external_wake_source(&t));
    t.state=NAS_TASK_BLOCKED;t.wait.pending_valid=false;
    assert(!nas_task_wait_has_external_wake_source(&t));
    t.wait.pending_valid=true;t.wait.blocked_op=NAS_SC_read;
    t.wait.blocked_args[0]=1;
    assert(nas_task_wait_has_external_wake_source(&t));
    terminal_waitable=false;
    assert(!nas_task_wait_has_external_wake_source(&t));
    terminal_waitable=true;t.wait.blocked_op=NAS_SC_readv;
    assert(nas_task_wait_has_external_wake_source(&t));
    t.wait.blocked_args[0]=2;
    assert(!nas_task_wait_has_external_wake_source(&t));
    t.wait.blocked_op=NAS_SC_poll;t.wait.poll_tty_read=true;
    assert(nas_task_wait_has_external_wake_source(&t));
    t.wait.poll_tty_read=false;
    assert(!nas_task_wait_has_external_wake_source(&t));
    t.wait.blocked_op=NAS_SC_epoll_wait;
    assert(nas_task_wait_has_external_wake_source(&t));
    t.wait.blocked_op=NAS_SC_nanosleep;t.wait.deadline_active=true;
    assert(nas_task_wait_has_external_wake_source(&t));
    t.wait.deadline_active=false;
    assert(!nas_task_wait_has_external_wake_source(&t));
    puts("[WAIT IDLE PASS] TTY read/readv, poll, epoll, deadline and nonwaiters");
    return 0;
}
