#ifndef NASEED_FS_SOCKET_SYSCALL_H
#define NASEED_FS_SOCKET_SYSCALL_H
#include <stdbool.h>
#include "syscall.h"
/* AF_UNIX socket ABI. Return handled=false outside this module's syscall set. */
struct nas_fs_result nas_fs_socket_syscall(enum nas_syscall_op op,
                                           const uint64_t *args);
/* Netlink datagrams block on the original held description. The adapter
 * imports the complete Linux recvmsg/recvfrom metadata before Task parking;
 * the scheduler never reopens a possibly reused integer fd. */
struct nas_netlink_receive_attempt {
    bool applicable;
    void *pending;
    unsigned slot;
    uint64_t result;
};
void nas_fs_netlink_receive_start(enum nas_syscall_op op,const uint64_t *args,
                                   struct nas_netlink_receive_attempt *out);
uint64_t nas_fs_netlink_receive_resume(void *pending);
void nas_fs_netlink_receive_release(void *pending);
/* Native TCP sendmsg is a vector stream write. Validate its msghdr before
 * dispatching through the held-OpenFile, blocking writev executor.
 * Returns 1 for TCP with iov/count, 0 for other families, or -errno. */
int nas_fs_socket_tcp_sendmsg_iov(uint64_t fd,uint64_t message,
                                 uint64_t flags,uint64_t *iov,
                                 uint64_t *count);
/* Retry on the pinned original listener description after Task wakeup. */
uint64_t nas_fs_socket_accept_description(unsigned listener_slot,
                                           uint64_t address,
                                           uint64_t length_ptr,uint64_t flags);
/* A parked bind/connect/listen holds the original OpenFile description;
 * completion consumes its native worker result without re-submitting it. */
bool nas_fs_socket_control_ready(unsigned slot);
uint64_t nas_fs_socket_control_description(unsigned slot);
/* Consume a genuine upstream TCP_INFO snapshot and serialize its Linux ABI
 * in the original Task address space after its held OpenFile completes. */
uint64_t nas_fs_socket_tcp_info_description(unsigned slot,uint64_t address,
                                             uint64_t length_ptr);
/* Worker-owned native GETNAME result is copied back after Task wake. */
uint64_t nas_fs_socket_name_description(unsigned slot,uint64_t address,
                                        uint64_t length_ptr,bool peer);
#endif
