#ifndef NASEED_FS_SOCKET_SYSCALL_H
#define NASEED_FS_SOCKET_SYSCALL_H
#include "syscall.h"
/* AF_UNIX socket ABI. Return handled=false outside this module's syscall set. */
struct nas_fs_result nas_fs_socket_syscall(enum nas_syscall_op op,
                                           const uint64_t *args);
/* Retry on the pinned original listener description after Task wakeup. */
uint64_t nas_fs_socket_accept_description(unsigned listener_slot,
                                           uint64_t address,
                                           uint64_t length_ptr,uint64_t flags);
#endif
