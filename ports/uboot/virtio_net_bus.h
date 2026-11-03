#ifndef NASEED_UBOOT_VIRTIO_NET_BUS_H
#define NASEED_UBOOT_VIRTIO_NET_BUS_H
/* Class binding for both MMIO and PCI; no architecture-specific NIC logic. */
#include <stdint.h>
struct nas_virtio_net_irq_stats {
 uint64_t irq_acks,irq_rx_wakes,rx_completed;
 unsigned irq_driven_nics,irq_proven_nics;
};
int nas_virtio_net_driver_register(void);
unsigned nas_virtio_net_count(void);
/* Snapshot counters of live NIC owners; must be called in IRQ-masked kernel
 * context, as for the rest of the device registry on today's single CPU. */
void nas_virtio_net_irq_totals(struct nas_virtio_net_irq_stats *);
#endif
