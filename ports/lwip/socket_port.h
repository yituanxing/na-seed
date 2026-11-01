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
/* Pure admission check: a native closing Netconn retains its old indexed
 * bridge identity beyond FileOps.release(). Other File slots remain usable. */
bool nas_lwip_socket_slot_reusable(unsigned slot);
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
/* Datagram I/O through the SAME socket OpenFile and native bridge. Both
 * APIs copy kernel-owned buffers only; user iovecs/sockaddr are handled by
 * the Linux syscall adapter. One SEND is one UDP packet, never split into
 * multiple stream writes. Returns -EAGAIN while native work is pending. */
int64_t nas_lwip_socket_sendto(unsigned slot,const uint8_t *bytes,
                               uint16_t length,
                               struct nas_lwip_bridge_address destination);
int64_t nas_lwip_socket_recvfrom(unsigned slot,uint8_t *bytes,
                                 uint16_t capacity,
                                 struct nas_lwip_bridge_address *source,
                                 bool *truncated);

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
int nas_lwip_socket_buffer_capacity(unsigned slot,bool sending);
bool nas_lwip_socket_is_connected(unsigned slot);
bool nas_lwip_socket_is_listening(unsigned slot);
/* TCP recvfrom() can report the established peer without another native
 * request. Active CONNECT caches its target after successful completion;
 * the syscall adapter passes an accepted child's REAL bridge peer after
 * adoption. UDP packet sources are not inferred from a connected target.
 * snapshot returns 0 on established TCP, -ENOTCONN if no TCP peer is known,
 * and -EOPNOTSUPP for UDP. The setter is idempotent only for an equal peer. */
int nas_lwip_socket_peer_snapshot(unsigned slot,
                                  struct nas_lwip_bridge_address *out);
int nas_lwip_socket_port_set_peer(unsigned slot,
                                  struct nas_lwip_bridge_address peer);
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
int nas_lwip_socket_setbroadcast(unsigned slot,bool enabled);
/* SO_BINDTODEVICE is a real native PCB constraint via netconn_bind_if().
 * Caller maps the existing Linux physical NIC name to lwIP's actual index;
 * the successful index is shared by dup/fork via the same OpenFile slot. */
int nas_lwip_socket_bind_if(unsigned slot,unsigned index);
int nas_lwip_socket_bound_ifindex(unsigned slot);
/* TCP_NODELAY is changed on the real upstream TCP PCB by tcpip_thread.
 * Reads reflect the last completed native option mutation. */
int nas_lwip_socket_tcp_nodelay(unsigned slot,bool write,bool *enabled);
/* Linux IP_RECVERR is stored on the shared OpenFile (also across dup/fork).
 * Successful UDP paths can opt in; asynchronous ICMP MSG_ERRQUEUE delivery
 * is not implemented and must not be advertised as full parity. */
int nas_lwip_socket_ip_recverr(unsigned slot,bool write,bool *enabled);
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
