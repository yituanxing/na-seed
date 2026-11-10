#ifndef NAS_UBOOT_ETH_NET_H
#define NAS_UBOOT_ETH_NET_H
#include "dm.h"
/* Class-specific U-Boot Ethernet surface; all packet and queue algorithms
 * remain in pinned original driver source. The NaSeed netdev port, not
 * this compatibility header, owns network-device registration. */
struct eth_pdata { uint8_t enetaddr[6]; };
struct eth_ops {
 int (*start)(struct udevice *);
 int (*send)(struct udevice *,void *,int);
 int (*recv)(struct udevice *,int,uchar **);
 int (*free_pkt)(struct udevice *,uchar *,int);
 void (*stop)(struct udevice *);
 int (*write_hwaddr)(struct udevice *);
 int (*read_rom_hwaddr)(struct udevice *);
};
#endif
