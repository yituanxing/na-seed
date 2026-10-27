#ifndef NASEED_VIRTIO_MMIO_H
#define NASEED_VIRTIO_MMIO_H
#include "transport.h"
/* Platform owns mapped register lifetime; this backend stores it only as
 * transport context. No device-specific storage requests enter MMIO.
 */
int nas_virtio_mmio_transport_init(struct nas_virtio_transport *out,
                                  volatile uint32_t *mmio,
                                  const struct nas_dma_provider *dma);
#endif
