#ifndef NASEED_UBOOT_VIRTIO_NET_BUS_H
#define NASEED_UBOOT_VIRTIO_NET_BUS_H
/* Class binding for both MMIO and PCI; no architecture-specific NIC logic. */
int nas_virtio_net_driver_register(void);
unsigned nas_virtio_net_count(void);
#endif
