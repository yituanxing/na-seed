#ifndef NASEED_LWIP_SOCKET_PORT_H
#define NASEED_LWIP_SOCKET_PORT_H

#include "netconn_bridge.h"

struct nas_file_ops;

/* Opt-in AF_INET File provider. Its index is the kernel's *existing* global
 * OpenFile slot, never a second socket FD table. The caller owns a live File
 * reference (or nas_fd_slot_lease) before opening/adopting a connection, then
 * installs nas_lwip_socket_port_ops() on that description. */
const struct nas_file_ops *nas_lwip_socket_port_ops(void);
int nas_lwip_socket_port_open(unsigned slot,enum nas_lwip_bridge_type type);
int nas_lwip_socket_port_adopt(struct nas_lwip_bridge_id accepted);

/* Socket-specific control plane: the syscall adapter parses sockaddr and
 * reserves ACCEPT's child OpenFile lease; the provider only sees kernel
 * data. begin() queues once; complete() returns 0 while pending, 1 after
 * transferring its result, or negative errno. ACCEPT's result.accepted is
 * transferred to the caller, which must adopt or release that bridge ID.
 * Do not issue begin() twice while a command is in progress. */
int nas_lwip_socket_port_begin(unsigned slot,
                               const struct nas_lwip_bridge_request *request);
int nas_lwip_socket_port_complete(unsigned slot,
                                  struct nas_lwip_bridge_result *result);
/* Consume a deferred asynchronous send failure (SO_ERROR style). */
int nas_lwip_socket_port_take_error(unsigned slot);

/* Narrow syscall-facing shorthand. All operations queue native work; no
 * caller ever executes a Netconn function or waits on a native semaphore.
 * A second caller cannot subscribe to the one-shot completion of the
 * original Task: duplicate in-flight requests return -EALREADY.
 * control_finish returns the native status once; -EINPROGRESS means the
 * command is not finished, -EINVAL means there is no outstanding command.
 */
int nas_lwip_socket_open(unsigned slot,enum nas_lwip_bridge_type type);
int nas_lwip_socket_open_ready(unsigned slot); /* 1 ready, 0 pending, <0 error */
bool nas_lwip_socket_is_slot(unsigned slot);
int nas_lwip_socket_type(unsigned slot); /* TCP/UDP enum, or -EBADF */
bool nas_lwip_socket_is_connected(unsigned slot);
bool nas_lwip_socket_is_listening(unsigned slot);
int nas_lwip_socket_bind(unsigned slot,struct nas_lwip_bridge_address address);
int nas_lwip_socket_connect(unsigned slot,struct nas_lwip_bridge_address address);
int nas_lwip_socket_listen(unsigned slot,unsigned backlog);
int nas_lwip_socket_control_ready(unsigned slot);
int nas_lwip_socket_control_finish(unsigned slot);

#endif
