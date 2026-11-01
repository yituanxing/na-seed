#ifndef NASEED_LWIP_NETWORK_BOOT_H
#define NASEED_LWIP_NETWORK_BOOT_H

/* Linux socket options name the actual network device (eth0/en0), not
 * lwIP's internal netif prefix (ns0). Only the physical NIC attached by
 * native bootstrap may be selected; index zero is never a live interface. */
#define NAS_LWIP_DEVICE_NAME_MAX 16U /* Linux IFNAMSIZ */
int nas_lwip_guest_ifindex(const char *device_name); /* positive or -ENODEV */
int nas_lwip_guest_ifname(unsigned index,
                         char output[NAS_LWIP_DEVICE_NAME_MAX]);
/* Snapshot usable IPv4 interfaces for Linux's read-only SIOCGIFCONF ABI.
 * The loopback endpoint exists in lwIP; the physical NIC is exposed only
 * after its actual DHCP-supplied IPv4 address was observed on tcpip_thread. */
struct nas_lwip_guest_ipv4_if {
 char name[NAS_LWIP_DEVICE_NAME_MAX];
 unsigned ipv4_host_order;
};
unsigned nas_lwip_guest_ipv4_ifconf(
    struct nas_lwip_guest_ipv4_if out[2]);

#endif
