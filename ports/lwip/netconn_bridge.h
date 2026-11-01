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
#define NAS_LWIP_BRIDGE_ACTIVE_LIMIT 8U /* matches lwIP MEMP_NUM_NETCONN */
#define NAS_LWIP_BRIDGE_QUEUE 8U
#define NAS_LWIP_BRIDGE_DATA 512U

struct nas_lwip_bridge_id { uint16_t slot; uint32_t generation; };
struct nas_lwip_bridge_address { uint32_t ipv4; uint16_t port; };
enum nas_lwip_bridge_type { NAS_LWIP_BRIDGE_TCP=1, NAS_LWIP_BRIDGE_UDP=2 };
enum nas_lwip_bridge_op {
 NAS_LWIP_BRIDGE_BIND=1, NAS_LWIP_BRIDGE_CONNECT,
 NAS_LWIP_BRIDGE_LISTEN, NAS_LWIP_BRIDGE_ACCEPT,
 NAS_LWIP_BRIDGE_SEND, NAS_LWIP_BRIDGE_RECV
};
struct nas_lwip_bridge_request {
 enum nas_lwip_bridge_op op;
 struct nas_lwip_bridge_address address;
 uint16_t accept_slot; /* caller-reserved OpenFile slot for ACCEPT */
 uint16_t backlog;
 uint16_t length;
 const uint8_t *bytes; /* copied on submit, never retained */
};
struct nas_lwip_bridge_result {
 int status; /* 0 or negative Linux errno from original lwIP err_t */
 uint16_t length; /* accepted send / received bytes, up to DATA */
 bool truncated; /* UDP datagram larger than DATA was consumed, not hidden */
 uint8_t bytes[NAS_LWIP_BRIDGE_DATA];
 struct nas_lwip_bridge_address peer;
 struct nas_lwip_bridge_id accepted; /* ACCEPT transfers an owned bridge ID */
};

bool nas_lwip_bridge_start(void); /* call after tcpip_init; spawns native worker */
int nas_lwip_bridge_open(unsigned open_file_slot,
                         enum nas_lwip_bridge_type type,
                         struct nas_lwip_bridge_id *out);
int nas_lwip_bridge_submit(struct nas_lwip_bridge_id id,
                           const struct nas_lwip_bridge_request *request);
/* 1 = result consumed, 0 = pending/no completion, negative = stale/closed. */
int nas_lwip_bridge_take(struct nas_lwip_bridge_id id,
                         struct nas_lwip_bridge_result *out);
/* Asynchronous final File close. Invalidates ID immediately; a queued
 * command is canceled, or an already-run command finishes before native
 * netconn_delete. Never blocks user tasks. */
int nas_lwip_bridge_release(struct nas_lwip_bridge_id id);
bool nas_lwip_bridge_alive(struct nas_lwip_bridge_id id);
#endif
