#ifndef NASEED_LWIP_SOCKET_PORT_H
#define NASEED_LWIP_SOCKET_PORT_H

#include "netconn_bridge.h"
#include "../../kernel/fd.h"

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
/* Native local/peer address queries and supported control operations remain
 * asynchronous. GETNAME has an address-bearing result; the other two share
 * control_ready/control_finish with bind/listen. */
int nas_lwip_socket_getname_begin(unsigned slot,bool peer);
int nas_lwip_socket_getname_complete(unsigned slot,
                                     struct nas_lwip_bridge_address *address);
int nas_lwip_socket_shutdown(unsigned slot,unsigned how);
int nas_lwip_socket_setreuseaddr(unsigned slot,bool enabled);
int nas_lwip_socket_control_ready(unsigned slot);
int nas_lwip_socket_control_finish(unsigned slot);

/* The syscall adapter owns an OpenFile lease before native ACCEPT is
 * queued. No User pointer or task-local FD enters this layer. Poll readiness
 * avoids speculative accepts when lwIP reports an empty backlog.
 *
 * begin: 0 queued, -EAGAIN no pending peer / native queue backpressure;
 * complete: 0 still executing, 1 successful transfer of accepted+peer,
 * or negative native errno (including -EAGAIN for a raced-empty backlog).
 * cancel: invalidates the outstanding command before caller drops its lease;
 * late completions and orphan child Netconn handles are released on the
 * native worker rather than stranded in this File or a second FD table. */
int nas_lwip_socket_accept_begin(unsigned listener_slot,
                                  struct nas_fd_slot_lease lease);
int nas_lwip_socket_accept_complete(unsigned listener_slot,
                                     struct nas_fd_slot_lease lease,
                                     struct nas_lwip_bridge_id *accepted,
                                     struct nas_lwip_bridge_address *peer);
int nas_lwip_socket_accept_cancel(unsigned listener_slot,
                                   struct nas_fd_slot_lease lease);

/* A nonblocking listener has no sleeping Task to own its native ACCEPT.
 * Its shared OpenFile therefore retains one real kernel slot lease while
 * poll/select prefetches the next child. Only a genuinely readable native
 * listener may start prefetch. A completed child and its lease transfer
 * together to the syscall adapter, which adopts/commits them atomically.
 * This also lets a blocking accept harvest a ticket retained across F_SETFL.
 *
 * start: 0 queued/already active, -EAGAIN no native backlog/work capacity,
 *        -EALREADY a Task already owns this listener's ACCEPT.
 * prefetched: 0 no owned ticket, -EAGAIN pending, 1 completed transfer,
 *             other negative native errno after retiring its lease.
 * Caller never cancels the lease returned with 1 before releasing any
 * accepted bridge child, and never starts a Task-owned ACCEPT while a
 * prefetch ticket is active. */
int nas_lwip_socket_accept_prefetch_start(unsigned listener_slot);
int nas_lwip_socket_accept_prefetched(unsigned listener_slot,
                                      struct nas_fd_slot_lease *lease,
                                      struct nas_lwip_bridge_id *accepted,
                                      struct nas_lwip_bridge_address *peer);

#endif
