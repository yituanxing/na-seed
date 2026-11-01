/* Real Task wait dispatch for TCP recv(MSG_PEEK). The socket provider's
 * asynchronous RX completion is carried by a pinned File request; a blocked
 * recvfrom must use the same readiness probe as read() even though its
 * syscall number differs. This missed branch stranded Tinyproxy workers
 * until a shutdown signal in genuine M149 QEMU. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/task/wait.c"

static bool mock_rx_ready;
static struct nas_task *expected_waiter;
static struct nas_pending_io *expected_request;
uint64_t nas_arch_monotonic_ns(void) {return 0;}
struct nas_task *nas_task_at(unsigned index) {
    static struct nas_task unused;
    (void)index;
    return &unused;
}
bool nas_process_wait_owner(const struct nas_task *child,
                            const struct nas_task *waiter) {
    (void)child;(void)waiter;
    return false;
}
bool nas_process_waitable(const struct nas_task *child,uint64_t options,
                          const struct nas_task *waiter) {
    (void)child;(void)options;(void)waiter;
    return false;
}
bool nas_process_waitid_ready(const struct nas_task *waiter,uint64_t idtype,
                              uint64_t id,uint64_t options) {
    (void)waiter;(void)idtype;(void)id;(void)options;
    return false;
}
bool nas_signal_user_pending(const struct nas_task *task) {
    (void)task;
    return false;
}
bool nas_signal_timedwait_ready(const struct nas_task *task,uint64_t mask) {
    (void)task;(void)mask;
    return false;
}
bool nas_epoll_task_ready(const struct nas_task *task) {
    (void)task;
    return false;
}
bool nas_fs_socket_control_ready(unsigned slot) {
    (void)slot;
    return false;
}
bool nas_fs_poll_ready(const struct nas_task *task) {
    (void)task;
    return false;
}
bool nas_fs_io_ready(const struct nas_pending_io *pending,
                     const struct nas_task *task) {
    assert(task==expected_waiter && pending==expected_request);
    return mock_rx_ready;
}
int main(void) {
    struct nas_task task={0};
    uint64_t opaque_file_request=1;
    expected_waiter=&task;
    expected_request=(struct nas_pending_io *)&opaque_file_request;
    task.state=NAS_TASK_BLOCKED;
    task.resume_valid=true;
    task.wait.pending_valid=true;
    task.wait.pending_io=expected_request;
    task.wait.blocked_op=NAS_SC_recvfrom; /* Linux MSG_PEEK path */
    assert(!nas_task_wait_ready(&task));
    mock_rx_ready=true;
    assert(nas_task_wait_ready(&task)); /* actual ready RX wakes Task */
    task.wait.blocked_op=NAS_SC_read;
    assert(nas_task_wait_ready(&task)); /* original path unchanged */
    task.wait.blocked_op=NAS_SC_writev;
    assert(nas_task_wait_ready(&task)); /* sendmsg path unchanged */
    task.wait.blocked_op=NAS_SC_sendmsg;
    assert(!nas_task_wait_ready(&task)); /* no phantom pending operation */
    task.wait.blocked_op=NAS_SC_recvfrom;
    task.resume_valid=false;
    assert(!nas_task_wait_ready(&task)); /* no unowned wake */
    puts("[LWIP TCP PEEK WAKE PASS] real Task wait routing for recvfrom");
    return 0;
}
