#ifndef NASEED_FS_NETLINK_FILTER_H
#define NASEED_FS_NETLINK_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/* Linux NETLINK_NETFILTER (12): real, bounded nf_tables table registry.
 * Usercopy is the socket syscall adapter's responsibility. No rules, chains,
 * hooks or packet filtering are claimed by this provider. Unsupported mutating
 * operations return netlink errors; they never report invented success. */
#define NAS_NETLINK_FILTER_PROTOCOL 12U
#define NAS_NETLINK_FILTER_SOCKADDR_SIZE 12U
#define NAS_NETLINK_FILTER_MAX_DATAGRAM 4096U

struct nas_file_ops;
const struct nas_file_ops *nas_netlink_filter_ops(void);
bool nas_netlink_filter_is_slot(unsigned slot);
int nas_netlink_filter_open(unsigned slot,uint32_t owner_pid);
int nas_netlink_filter_protocol(unsigned slot);
int nas_netlink_filter_bind(unsigned slot,const uint8_t *address,unsigned length);
int nas_netlink_filter_getsockname(unsigned slot,
                                  uint8_t address[NAS_NETLINK_FILTER_SOCKADDR_SIZE]);
int nas_netlink_filter_set_sndbuf(unsigned slot,uint32_t requested);
int nas_netlink_filter_get_sndbuf(unsigned slot,uint32_t *bytes);
int nas_netlink_filter_set_rcvbuf(unsigned slot,uint32_t requested);
int nas_netlink_filter_get_rcvbuf(unsigned slot,uint32_t *bytes);
int64_t nas_netlink_filter_sendto(unsigned slot,const uint8_t *bytes,
                                 unsigned length,const uint8_t *destination,
                                 unsigned destination_length);
int64_t nas_netlink_filter_recvmsg(unsigned slot,uint8_t *destination,
                                  unsigned capacity,unsigned flags,
                                  uint8_t source[NAS_NETLINK_FILTER_SOCKADDR_SIZE],
                                  uint32_t *source_length,uint32_t *msg_flags);
#endif
