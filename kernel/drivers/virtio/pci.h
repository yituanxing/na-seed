#ifndef NAS_VIRTIO_PCI_TRANSPORT_H
#define NAS_VIRTIO_PCI_TRANSPORT_H
/* Modern PCI capabilities, shared by all ISAs. Enumeration and BAR mapping
 * belong to the platform; no block requests or filesystem logic here. */
#include "transport.h"
#include <stdint.h>
#include <stddef.h>
#define NAS_VIRTIO_PCI_BAR_COUNT 6U
#define NAS_VIRTIO_PCI_MAX_QUEUES 64U
struct nas_virtio_pci_bar {
    volatile uint8_t *cpu;
    uint64_t size;
};
struct nas_virtio_pci_resources {
    const uint8_t *config; /* Bounded 256-byte PCI config snapshot. */
    struct nas_virtio_pci_bar bars[NAS_VIRTIO_PCI_BAR_COUNT];
};
struct nas_virtio_pci {
    volatile uint8_t *common,*notify,*device;
    uint32_t device_size,notify_size,notify_multiplier;
    volatile uint16_t *queue_notify[NAS_VIRTIO_PCI_MAX_QUEUES];
};
int nas_virtio_pci_transport_init(struct nas_virtio_transport *out,
                                  struct nas_virtio_pci *pci,
                                  const struct nas_virtio_pci_resources *resources,
                                  const struct nas_dma_provider *dma);
#endif
