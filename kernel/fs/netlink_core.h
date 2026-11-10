#ifndef NASEED_FS_NETLINK_CORE_H
#define NASEED_FS_NETLINK_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* Protocol-neutral kernel Netlink dispatch. The Linux syscall adapter owns
 * user pointers, fd publication and sockaddr_nl/msghdr wire layout. Providers
 * own their bound ports, reply queues and protocol-specific validation. */
#define NAS_NETLINK_SOCKADDR_SIZE 12U
#define NAS_NETLINK_MAX_DATAGRAM 4096U
#define NAS_NETLINK_ROUTE_PROTOCOL 0U
#define NAS_NETLINK_SOCK_DIAG_PROTOCOL 4U

struct nas_file_ops;
bool nas_netlink_core_supports(unsigned protocol);
bool nas_netlink_core_is_slot(unsigned slot);
int nas_netlink_core_open(unsigned slot,uint32_t owner_pid,unsigned protocol,
                          const struct nas_file_ops **file_ops);
int nas_netlink_core_protocol(unsigned slot);
int nas_netlink_core_bind(unsigned slot,const uint8_t *addr,unsigned length);
int nas_netlink_core_getsockname(unsigned slot,
                                 uint8_t address[NAS_NETLINK_SOCKADDR_SIZE]);
int nas_netlink_core_set_sndbuf(unsigned slot,uint32_t requested);
int nas_netlink_core_get_sndbuf(unsigned slot,uint32_t *bytes);
int nas_netlink_core_set_rcvbuf(unsigned slot,uint32_t requested);
int nas_netlink_core_get_rcvbuf(unsigned slot,uint32_t *bytes);
int64_t nas_netlink_core_sendto(unsigned slot,const uint8_t *bytes,
                               unsigned length,const uint8_t *destination,
                               unsigned destination_length);
int64_t nas_netlink_core_recvmsg(unsigned slot,uint8_t *destination,
                                 unsigned capacity,unsigned flags,
                                 uint8_t source[NAS_NETLINK_SOCKADDR_SIZE],
                                 uint32_t *source_length,uint32_t *msg_flags);

#endif
