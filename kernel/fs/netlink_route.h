#ifndef NASEED_FS_NETLINK_ROUTE_H
#define NASEED_FS_NETLINK_ROUTE_H
#include <stdbool.h>
#include <stdint.h>

/* Kernel-buffer-only Linux NETLINK_ROUTE subset. The syscall adapter owns
 * every copyin/copyout and must preflight writable user memory before it
 * consumes a queued datagram. All three supported Linux ISAs use the same
 * little-endian sockaddr_nl/nlmsghdr wire layout. */
#define NAS_NETLINK_ROUTE_SOCKADDR_SIZE 12U
#define NAS_NETLINK_ROUTE_MAX_DATAGRAM 4096U

struct nas_file_ops;
const struct nas_file_ops *nas_netlink_route_ops(void);
bool nas_netlink_route_is_slot(unsigned slot);
int nas_netlink_route_open(unsigned slot,uint32_t owner_pid);
int nas_netlink_route_bind(unsigned slot,const uint8_t *address,unsigned length);
int nas_netlink_route_getsockname(unsigned slot,
                                 uint8_t address[NAS_NETLINK_ROUTE_SOCKADDR_SIZE]);
int64_t nas_netlink_route_sendto(unsigned slot,const uint8_t *bytes,
                                unsigned length,const uint8_t *destination,
                                unsigned destination_length);
/* recvmsg returns bytes copied, except MSG_TRUNC as an INPUT flag requests
 * the complete datagram length even if capacity is smaller. msg_flags
 * reports MSG_TRUNC when actual datagram data did not fit. source is the
 * truthful AF_NETLINK kernel endpoint (nl_pid=0), not a user pointer. */
int64_t nas_netlink_route_recvmsg(unsigned slot,uint8_t *destination,
                                 unsigned capacity,unsigned flags,
                                 uint8_t source[NAS_NETLINK_ROUTE_SOCKADDR_SIZE],
                                 uint32_t *source_length,uint32_t *msg_flags);
#endif
