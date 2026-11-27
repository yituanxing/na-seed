#ifndef NASEED_FS_NETLINK_KOBJECT_H
#define NASEED_FS_NETLINK_KOBJECT_H

#include <stdbool.h>
#include <stdint.h>

/* Kernel-owned NETLINK_KOBJECT_UEVENT (15). A monitor is a real, bound
 * Netlink socket. Kernel uevents may be delivered only for actual device
 * transitions; monitor installation itself never synthesizes a hotplug. */
struct nas_file_ops;
const struct nas_file_ops *nas_netlink_kobject_ops(void);
bool nas_netlink_kobject_is_slot(unsigned slot);
int nas_netlink_kobject_open(unsigned slot,uint32_t owner_pid);
int nas_netlink_kobject_protocol(unsigned slot);
int nas_netlink_kobject_bind(unsigned slot,const uint8_t *address,unsigned length);
int nas_netlink_kobject_getsockname(unsigned slot,uint8_t address[12]);
int nas_netlink_kobject_set_sndbuf(unsigned slot,uint32_t requested);
int nas_netlink_kobject_get_sndbuf(unsigned slot,uint32_t *bytes);
int nas_netlink_kobject_set_rcvbuf(unsigned slot,uint32_t requested);
int nas_netlink_kobject_get_rcvbuf(unsigned slot,uint32_t *bytes);
int nas_netlink_kobject_set_passcred(unsigned slot,bool value);
int nas_netlink_kobject_get_passcred(unsigned slot,bool *value);
int64_t nas_netlink_kobject_sendto(unsigned slot,const uint8_t *bytes,
    unsigned length,const uint8_t *destination,unsigned destination_length);
int64_t nas_netlink_kobject_recvmsg(unsigned slot,uint8_t *destination,
    unsigned capacity,unsigned flags,uint8_t source[12],
    uint32_t *source_length,uint32_t *msg_flags);
#endif
