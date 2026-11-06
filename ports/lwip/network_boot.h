#ifndef NASEED_LWIP_NETWORK_BOOT_H
#define NASEED_LWIP_NETWORK_BOOT_H
#include <stdbool.h>
#include <stdint.h>
#include "../../kernel/drivers/port/net.h"

/* Linux socket options name the actual network device (eth0/en0), not
 * lwIP's internal netif prefix (ns0). Any presently attached physical NIC
 * may be selected; index zero is never a live interface. */
#define NAS_LWIP_DEVICE_NAME_MAX 16U /* Linux IFNAMSIZ */
/* One coherent, read-only view of interfaces genuinely owned by the live
 * upstream stack. Physical device identity comes from the pinned netdev;
 * ifindex, administrative/link state and IPv4 lease come from lwIP. A NIC
 * without DHCP is still a device, but does not have an IPv4 address.
 * This is an interface projection, never another device registry. */
#define NAS_LWIP_GUEST_IF_LIMIT (NAS_NETDEV_LIMIT + 1U)
struct nas_lwip_guest_if {
 char name[NAS_LWIP_DEVICE_NAME_MAX];
 unsigned ifindex;
 uint32_t ipv4_host_order;
 uint32_t mtu;
 uint8_t prefix_len;
 bool up,link_up,loopback;
};
unsigned nas_lwip_guest_if_snapshot(
    struct nas_lwip_guest_if out[NAS_LWIP_GUEST_IF_LIMIT]);
/* Traffic control is a separately owned, live attachment: an interface
 * snapshot alone does not imply that a Linux qdisc exists. Today only the
 * upstream lwIP loopif has an attached noqueue/direct-output policy; each
 * physical NIC remains unreported until it owns a real TC discipline. */
struct nas_lwip_guest_qdisc {
 uint32_t ifindex;
 enum nas_net_qdisc_kind kind;
};
unsigned nas_lwip_guest_qdisc_snapshot(
    struct nas_lwip_guest_qdisc out[NAS_LWIP_GUEST_IF_LIMIT]);
int nas_lwip_guest_ifindex(const char *device_name); /* positive or -ENODEV */
int nas_lwip_guest_ifname(unsigned index,
                         char output[NAS_LWIP_DEVICE_NAME_MAX]);
/* Snapshot usable IPv4 interfaces for Linux's read-only SIOCGIFCONF ABI.
 * The loopback endpoint exists in lwIP; each physical NIC is exposed only
 * after its actual DHCP-supplied IPv4 address was observed on tcpip_thread. */
struct nas_lwip_guest_ipv4_if {
 char name[NAS_LWIP_DEVICE_NAME_MAX];
 unsigned ifindex; /* same core snapshot as the address, never re-resolve */
 unsigned ipv4_host_order;
 uint8_t prefix_len; /* DHCP's actual contiguous IPv4 netmask length */
 uint32_t mtu; /* native core netif MTU, not a presumed Ethernet default */
};
unsigned nas_lwip_guest_ipv4_ifconf(
    struct nas_lwip_guest_ipv4_if out[NAS_LWIP_GUEST_IF_LIMIT]);

/* Published lwIP DNS for the same genuinely leased interface
 * that owns the current lwIP default route. This read-only projection is
 * safe from other kernel contexts; only tcpip_thread reads/writes lwIP
 * internals, and publication is one atomic 64-bit value. No lease means
 * zero entries. Upstream does not expose ACK option-6 presence, so a DHCP
 * ACK omitting DNS may leave stale lwIP global servers; no stronger claim
 * about option-6 provenance is made by this API. */
#define NAS_LWIP_DNS_SERVER_LIMIT 2U
unsigned nas_lwip_guest_dns_snapshot(
    uint32_t out[NAS_LWIP_DNS_SERVER_LIMIT]);

#endif
