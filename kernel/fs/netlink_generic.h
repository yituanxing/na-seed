#ifndef NASEED_FS_NETLINK_GENERIC_H
#define NASEED_FS_NETLINK_GENERIC_H

#include <stdbool.h>
#include <stdint.h>

/* NETLINK_GENERIC is protocol 16 within AF_NETLINK (also 16).  This
 * provider registers only the controller family `nlctrl`: discovering
 * unimplemented Linux generic families must return ENOENT, never a
 * fabricated family ID.  The socket syscall adapter owns usercopy/FDs. */
#define NAS_NETLINK_GENERIC_PROTOCOL 16U
#define NAS_NETLINK_GENERIC_SOCKADDR_SIZE 12U
#define NAS_NETLINK_GENERIC_MAX_DATAGRAM 4096U

struct nas_file_ops;
const struct nas_file_ops *nas_netlink_generic_ops(void);
bool nas_netlink_generic_is_slot(unsigned slot);
int nas_netlink_generic_open(unsigned slot,uint32_t owner_pid);
int nas_netlink_generic_protocol(unsigned slot);
int nas_netlink_generic_bind(unsigned slot,const uint8_t *address,unsigned length);
int nas_netlink_generic_getsockname(unsigned slot,
                                   uint8_t address[NAS_NETLINK_GENERIC_SOCKADDR_SIZE]);
int nas_netlink_generic_set_sndbuf(unsigned slot,uint32_t requested);
int nas_netlink_generic_get_sndbuf(unsigned slot,uint32_t *bytes);
int nas_netlink_generic_set_rcvbuf(unsigned slot,uint32_t requested);
int nas_netlink_generic_get_rcvbuf(unsigned slot,uint32_t *bytes);
int64_t nas_netlink_generic_sendto(unsigned slot,const uint8_t *bytes,
                                  unsigned length,const uint8_t *destination,
                                  unsigned destination_length);
int64_t nas_netlink_generic_recvmsg(unsigned slot,uint8_t *destination,
                                   unsigned capacity,unsigned flags,
                                   uint8_t source[NAS_NETLINK_GENERIC_SOCKADDR_SIZE],
                                   uint32_t *source_length,uint32_t *msg_flags);
#endif
