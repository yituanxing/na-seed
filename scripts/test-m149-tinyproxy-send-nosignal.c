/* Tinyproxy 1.11.3 calls send(MSG_NOSIGNAL) for each forwarded HTTP header.
 * Exercise the REAL syscall dispatch gate and Task parked-I/O continuation:
 * a blocked TCP send retains its original File identity across FD reuse.
 * The lwIP provider's real PORT_TX admission/backpressure is covered by
 * test-lwip-socket-port.c; this fixture owns syscall routing and lifetime. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../kernel/syscall/dispatch.c"

struct nas_pending_io { unsigned held_slot; uint64_t expected_length; };
static struct nas_task task;
static struct nas_fd_table fd_table;
struct nas_task *nas_current_task=&task;
static struct nas_pending_io held={.held_slot=17,.expected_length=5};
static nas_wait_finish_fn finish_callback;
static unsigned send_started,send_resumed,send_released;
static unsigned sigpipe_signaled,sigpipe_default_checks,child_exits;
static unsigned blocked_count;
static bool port_tx_busy=true;

bool nas_lwip_socket_is_slot(unsigned slot) { return slot==17 || slot==27; }
int nas_lwip_socket_type(unsigned slot) {
    if(slot==17)return NAS_LWIP_BRIDGE_TCP;
    if(slot==27)return NAS_LWIP_BRIDGE_UDP;
    return -EBADF;
}
void nas_fs_io_start(uint64_t nr,uint64_t fd,uint64_t address,uint64_t len,
                     struct nas_io_attempt *attempt) {
    assert(nr==NAS_SC_write && fd==5 && address==0x1000 && len==5);
    assert(task.fds->entries[fd].slot==17);
    send_started++;
    *attempt=(struct nas_io_attempt){.pipe_like=true};
    if(port_tx_busy) {
        attempt->pending=&held;
        attempt->result=(uint64_t)-EOPNOTSUPP;
    } else attempt->result=(uint64_t)-EPIPE;
}
uint64_t nas_fs_io_resume(struct nas_pending_io *pending) {
    assert(pending==&held && pending->held_slot==17);
    /* The integer FD now identifies a different UDP OpenFile. Completion
     * must use the originally held TCP slot, never re-resolve fd 5. */
    assert(task.fds->entries[5].slot==27);
    send_resumed++;
    return pending->expected_length;
}
void nas_fs_io_dispose(struct nas_pending_io *pending) {
    assert(pending==&held);send_released++;
}
bool nas_fs_io_tty_read(const struct nas_pending_io *pending) {
    (void)pending;return false;
}
void nas_fs_io_read_wait_start(const struct nas_pending_io *pending,
                               struct nas_task *task_ptr) {
    (void)pending;(void)task_ptr;
}
void nas_tty_blocking_read_start(struct nas_task *task_ptr) {
    (void)task_ptr;assert(!"TCP send cannot enter TTY readiness");
}
uint64_t nas_task_block(uint64_t *frame,enum nas_syscall_op nr,
                        uint64_t fd,uint64_t address,uint64_t length,
                        uint64_t extra,nas_wait_finish_fn finish) {
    (void)frame;
    assert(nr==NAS_SC_write && fd==5 && address==0x1000 &&
           length==5 && extra==0 && finish);
    finish_callback=finish;blocked_count++;
    return UINT64_C(0x149149);
}
bool nas_sched_sigpipe_default(bool pipe_like) {
    assert(pipe_like);sigpipe_default_checks++;return true;
}
void nas_sched_child_exit(uint32_t code,uint32_t signal) {
    (void)code;(void)signal;child_exits++;
}
uint64_t nas_task_schedule(uint64_t *frame) {
    (void)frame;return UINT64_C(0xdead);
}
void nas_signal_broken_pipe(bool pipe_like) {
    assert(pipe_like);sigpipe_signaled++;
}

int main(void) {
    task.fds=&fd_table;
    fd_table.entries[5]=(struct nas_fd_entry){.kind=FD_SOCKET,.slot=17};
    uint64_t args[7]={0,5,0x1000,5,0x4000,0,0};
    assert(native_tcp_send_nosignal_wait(NAS_SC_sendto,args));
    assert(!native_tcp_send_nosignal_wait(NAS_SC_recvfrom,args));
    args[4]=0x8000; /* MSG_MORE is not this blocking-path contract */
    assert(!native_tcp_send_nosignal_wait(NAS_SC_sendto,args));
    args[4]=0x4001; /* unknown/additional flags cannot be ignored */
    assert(!native_tcp_send_nosignal_wait(NAS_SC_sendto,args));
    args[4]=0x4000;args[5]=0x2000;args[6]=16;
    assert(!native_tcp_send_nosignal_wait(NAS_SC_sendto,args));
    args[5]=args[6]=0;
    fd_table.entries[5].slot=27; /* UDP never takes the TCP wait route */
    assert(!native_tcp_send_nosignal_wait(NAS_SC_sendto,args));
    fd_table.entries[5].kind=FD_CLOSED;
    assert(!native_tcp_send_nosignal_wait(NAS_SC_sendto,args));
    fd_table.entries[5]=(struct nas_fd_entry){.kind=FD_SOCKET,.slot=17};
    assert(native_tcp_send_nosignal_wait(NAS_SC_sendto,args));

    uint64_t result=task_pipe_io(args,NAS_SC_write,5,0x1000,5,true);
    assert(result==UINT64_C(0x149149) && blocked_count==1 &&
           send_started==1 && task.wait.pending_io==&held &&
           task.wait.pending_io_release==nas_fs_io_dispose &&
           task.wait.suppress_sigpipe);
    fd_table.entries[5]=(struct nas_fd_entry){.kind=FD_SOCKET,.slot=27};
    assert(finish_callback(&task)==5 && send_resumed==1);
    nas_task_wait_dispose(&task);
    assert(send_released==1 && !task.wait.pending_io);

    /* Broken TCP peer + MSG_NOSIGNAL returns EPIPE but never raises SIGPIPE. */
    fd_table.entries[5].slot=17;
    port_tx_busy=false;
    result=task_pipe_io(args,NAS_SC_write,5,0x1000,5,true);
    assert((int64_t)result==-EPIPE && !sigpipe_signaled &&
           !sigpipe_default_checks && !child_exits);
    puts("[M149 TINYPROXY MSG_NOSIGNAL PASS] TCP park/ready, FD ABA, SIGPIPE");
    return 0;
}
