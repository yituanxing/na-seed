#ifndef NAS_UBOOT_NET_PORT_H
#define NAS_UBOOT_NET_PORT_H
/* The pinned U-Boot virtio_net.c owns Ethernet protocol and its eth_ops.
 * This class adapter owns DMA-safe queues and NaSeed netdev lifecycle. */
#include "../../kernel/drivers/port/net.h"
#include "../../kernel/drivers/core/irq.h"
#include "../../kernel/drivers/virtio/transport.h"
#include "../../kernel/task/k_sync.h"
#include "../unikraft/split_queue.h"
#include "compat/net.h"
#include "compat/virtio.h"
#include "compat/virtio_ring.h"

#define NAS_UBOOT_NET_RX_SLOTS 32U
#define NAS_UBOOT_NET_RX_RING_SIZE 64U
#define NAS_UBOOT_NET_RX_BYTES 1526U
struct nas_uboot_net {
 struct nas_netdev netdev;
 struct nas_virtio_transport transport;
 struct nas_irq_action rx_irq;
 uint64_t irq_acks,irq_rx_wakes,rx_completed;
 uint64_t tx_irq_wakes,tx_worker_waits,tx_legacy_polls;
 struct nas_ksem tx_completion;
 struct nas_vring_queue rx_ring,tx_ring;
 struct nas_dma_page rx_dma[NAS_UBOOT_NET_RX_SLOTS],tx_dma;
 uint16_t rx_slot_by_head[NAS_UBOOT_NET_RX_RING_SIZE]; /* index + 1 */
 bool rx_queued[NAS_UBOOT_NET_RX_SLOTS];
 unsigned rx_dma_count,rx_submitted;
 uint16_t tx_head;
 struct virtio_dev_priv parent_priv;
 struct udevice parent,udev;
 struct eth_pdata eth;
 struct virtqueue rx_vq,tx_vq,*queue_refs[2];
 bool busy,poisoned,tx_pending,rx_retry_after_tx;
};
int nas_uboot_net_attach(struct nas_uboot_net *,
 const struct nas_virtio_transport *,const char *name,
 struct nas_irq_route irq);
int nas_uboot_net_detach(struct nas_uboot_net *);
#endif
