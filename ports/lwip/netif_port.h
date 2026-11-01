#ifndef NAS_LWIP_NETIF_PORT_H
#define NAS_LWIP_NETIF_PORT_H
/* Single adapter between official lwIP pbuf/netif and NaSeed netdev.
 * No IP parsing, ARP table, socket registry or duplicate NIC queue code. */
#include "../../kernel/drivers/port/net.h"
#include "lwip/netif.h"
struct nas_lwip_eth {
 struct netif interface;
 struct nas_netdev *device; /* pinned until core_detach */
 bool attached;
};
/* Call on the lwIP tcpip_thread/core callback, not from IRQ context.
 * Owner must stop its RX pump before core_detach. */
int nas_lwip_eth_core_attach(struct nas_lwip_eth *,const char *device_name,
                             bool dhcp);
int nas_lwip_eth_core_detach(struct nas_lwip_eth *);
/* Call from native network worker outside IRQ; tcpip_input enqueues to lwIP.
 * A link driver must never call this from its interrupt handler. */
int nas_lwip_eth_poll(struct nas_lwip_eth *,unsigned budget);
#endif
