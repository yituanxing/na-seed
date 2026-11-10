#ifndef NASEED_LWIP_NETCONN_BRIDGE_H
#define NASEED_LWIP_NETCONN_BRIDGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Netconn never runs on a user syscall C stack: submissions copy kernel
 * buffers, the sole native worker executes upstream lwIP API calls, and the
 * caller observes completion later through its owned File description.
 * This module deliberately contains no FD table or Linux sockaddr parser. */
#define NAS_LWIP_BRIDGE_SLOTS 128U /* indexed by NAS_OPEN_FILE_LIMIT */
#define NAS_LWIP_BRIDGE_ACTIVE_LIMIT 32U /* matches lwIP MEMP_NUM_NETCONN */
#define NAS_LWIP_BRIDGE_QUEUE 8U /* bounded native work queue, not socket count */
#define NAS_LWIP_BRIDGE_DATA 512U
#define NAS_LWIP_UDP_PENDING 8U /* additional complete datagrams per UDP File */
/* An asynchronous native close retains the old bridge generation even after
 * the former OpenFile refcount fell to zero; socket admission must not reuse
 * its indexed slot before native worker teardown completes. */
bool nas_lwip_bridge_slot_available(unsigned slot);

struct nas_lwip_bridge_id { uint16_t slot; uint32_t generation; };
struct nas_lwip_bridge_address { uint32_t ipv4; uint16_t port; };
/* RAW is IPv4 ICMP protocol 1, using lwIP's genuine RAW PCB. */
enum nas_lwip_bridge_type {
 NAS_LWIP_BRIDGE_TCP=1, NAS_LWIP_BRIDGE_UDP=2,
 NAS_LWIP_BRIDGE_RAW_ICMP=3
};
enum nas_lwip_bridge_op {
 NAS_LWIP_BRIDGE_BIND=1, NAS_LWIP_BRIDGE_CONNECT,
 NAS_LWIP_BRIDGE_LISTEN, NAS_LWIP_BRIDGE_ACCEPT,
 NAS_LWIP_BRIDGE_SEND, NAS_LWIP_BRIDGE_RECV,
 NAS_LWIP_BRIDGE_GETNAME, NAS_LWIP_BRIDGE_SHUTDOWN,
 NAS_LWIP_BRIDGE_SETREUSEADDR, NAS_LWIP_BRIDGE_SETBROADCAST,
 NAS_LWIP_BRIDGE_SETNODELAY, NAS_LWIP_BRIDGE_BIND_IF,
 NAS_LWIP_BRIDGE_SETKEEPALIVE, NAS_LWIP_BRIDGE_SETPKTINFO,
 NAS_LWIP_BRIDGE_GETTCPINFO
};
/* A factual snapshot from the upstream TCP PCB on tcpip_thread. This is
 * target-independent; Linux's tcp_info wire layout belongs to the syscall
 * adapter. Fields absent from lwIP are deliberately not claimed here. */
struct nas_lwip_tcp_metrics {
 uint32_t state,send_mss,receive_mss,send_window,receive_window;
 uint32_t congestion_window,slow_start_threshold,unacked_segments;
 uint32_t retransmission_timeout_us,smoothed_rtt_us,rtt_variance_us;
 uint32_t current_retransmissions;
};
struct nas_lwip_bridge_pktinfo {
 uint32_t ifindex; /* actual inbound lwIP netif index */
 uint32_t spec_dst; /* local source address usable in a reply, host order */
 uint32_t destination; /* original IPv4 header destination, host order */
};
/* A whole UDP datagram, never a TCP-style partial byte stream. The File
 * provider owns a bounded FIFO of these while the native worker is busy.
 * Final close transfers the remaining accepted packets into the bridge's
 * old generation, so closing a descriptor does not discard queued sends. */
struct nas_lwip_bridge_datagram {
 uint8_t bytes[NAS_LWIP_BRIDGE_DATA];
 uint16_t length;
 struct nas_lwip_bridge_address destination;
 bool send_pktinfo;
 struct nas_lwip_bridge_pktinfo packet_info;
};
struct nas_lwip_bridge_request {
 enum nas_lwip_bridge_op op;
 struct nas_lwip_bridge_address address;
 bool name_peer; /* GETNAME: false = local endpoint, true = peer */
 uint8_t shutdown_how; /* Linux SHUT_RD=0, SHUT_WR=1, SHUT_RDWR=2 */
 bool reuseaddr; /* SETREUSEADDR, applied to actual lwIP PCB on core */
 bool broadcast; /* SETBROADCAST, applied to actual lwIP UDP PCB on core */
 bool nodelay; /* SETNODELAY, applied to actual lwIP TCP PCB on core */
 bool keepalive; /* SETKEEPALIVE, actual TCP SOF_KEEPALIVE probe flag */
 bool pktinfo; /* SETPKTINFO, real upstream Netconn receive metadata flag */
 bool send_pktinfo; /* SEND has Linux per-datagram source/interface override */
 struct nas_lwip_bridge_pktinfo packet_info;
 uint8_t if_index; /* BIND_IF: real attached lwIP netif, never guessed */
 uint16_t accept_slot; /* caller-reserved OpenFile slot for ACCEPT */
 uint64_t accept_lease_token; /* authoritative nas_fd_slot_lease_begin token */
 uint16_t backlog;
 uint16_t length;
 const uint8_t *bytes; /* copied on submit, never retained */
};
struct nas_lwip_bridge_result {
 int status; /* 0 or negative Linux errno from original lwIP err_t */
 uint16_t length; /* accepted send / received bytes, up to DATA */
 bool truncated; /* UDP datagram larger than DATA was consumed, not hidden */
 bool local_valid; /* LISTEN: native netconn_getaddr verified local endpoint */
 uint8_t bytes[NAS_LWIP_BRIDGE_DATA];
 struct nas_lwip_bridge_address peer; /* ACCEPT/RECV peer; GETNAME endpoint */
 struct nas_lwip_bridge_address local; /* post-LISTEN native endpoint only */
 struct nas_lwip_bridge_pktinfo packet_info; /* actual UDP RX Netbuf metadata */
 struct nas_lwip_bridge_id accepted; /* ACCEPT transfers an owned bridge ID */
 struct nas_lwip_tcp_metrics tcp_metrics; /* GETTCPINFO: real PCB snapshot */
};

bool nas_lwip_bridge_start(void); /* call after tcpip_init; spawns native worker */
int nas_lwip_bridge_open(unsigned open_file_slot,
                         enum nas_lwip_bridge_type type,
                         struct nas_lwip_bridge_id *out);
int nas_lwip_bridge_submit(struct nas_lwip_bridge_id id,
                           const struct nas_lwip_bridge_request *request);
/* Completion notification is a nonblocking, same-kernel-stack callback.
 * The bridge does not inspect File state; its provider may consume the
 * finished result and enqueue the next owned datagram autonomously. */
void nas_lwip_bridge_set_completion_hook(
                     void (*hook)(struct nas_lwip_bridge_id));
/* 1 = result consumed, 0 = pending/no completion, negative = stale/closed. */
int nas_lwip_bridge_take(struct nas_lwip_bridge_id id,
                         struct nas_lwip_bridge_result *out);
/* Asynchronous final File close. Invalidates ID immediately; already
 * admitted TCP SEND bytes receive a bounded native drain before FIN.
 * `tail` is the provider's unsubmitted copied TX suffix (at most DATA),
 * not user memory; NULL/0 means any in-flight SEND is already bridge-owned.
 * Neither path waits on a user Task; delivery itself is not guaranteed. */
int nas_lwip_bridge_release_with_tail(struct nas_lwip_bridge_id id,
                                      const uint8_t *tail,uint16_t length);
/* `active` is the unsubmitted UDP datagram (NULL when the worker owns it).
 * `ring` is the provider's bounded FIFO with `first` as its oldest entry.
 * Both are copied into the old bridge generation before File destruction. */
int nas_lwip_bridge_release_with_udp(struct nas_lwip_bridge_id id,
                              const struct nas_lwip_bridge_datagram *active,
                              const struct nas_lwip_bridge_datagram *ring,
                              unsigned first,unsigned count);
int nas_lwip_bridge_release(struct nas_lwip_bridge_id id);
bool nas_lwip_bridge_alive(struct nas_lwip_bridge_id id);
/* lwIP acceptmbox events plus one worker-owned connection saved when a
 * caught signal cancelled its former accept lease after TCP dequeue.
 * Safe to inspect from Task wait/poll without calling Netconn. */
bool nas_lwip_bridge_listener_readable(struct nas_lwip_bridge_id listener);
struct nas_fd_slot_lease;
/* Cancel one exact leased ACCEPT, including any unclaimed child created
 * while the caller was interrupted. Call before releasing the File lease. */
int nas_lwip_bridge_cancel_accept(struct nas_lwip_bridge_id listener,
                                  const struct nas_fd_slot_lease *lease);
/* An interrupted accept may already have a child in SocketPort's completed
 * cache. Hand that unclaimed Netconn back to the listener, not the old FD
 * lease; the next accept will bind it to its own fresh leased OpenFile. */
int nas_lwip_bridge_park_accepted(struct nas_lwip_bridge_id listener,
                                  struct nas_lwip_bridge_id accepted);
#endif
