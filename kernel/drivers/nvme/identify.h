#ifndef NASEED_NVME_IDENTIFY_H
#define NASEED_NVME_IDENTIFY_H

#include <stdint.h>
#include "../port/dma.h"

/* A small, synchronous NVMe admin-only protocol core. This interface owns
 * neither PCI configuration, BAR mapping, interrupts nor DMA translation.
 * The caller must provide an already-qualified controller with MEM decode
 * and bus mastering enabled, and writable UC/NX MMIO. H3 enables this ONLY
 * on a dedicated QEMU fixture, never on an unknown physical controller. */
struct nas_nvme_io {
    void *context;
    uint32_t mmio_bytes;
    int (*read32)(void *context, uint32_t offset, uint32_t *value);
    int (*write32)(void *context, uint32_t offset, uint32_t value);
};

struct nas_nvme_identity {
    uint32_t version;
    uint16_t vendor_id;
    uint32_t namespace_count;
    char serial[21];
    char model[41];
};

/* Identify Controller (CNS=1) with polling CQ and 4KiB PRP. The NVMe
 * controller is disabled and queues are quiesced BEFORE DMA pages are freed.
 * If quiescence cannot be proven, the pages are deliberately retained. */
int nas_nvme_identify_controller(const struct nas_nvme_io *io,
                                  const struct nas_dma_provider *dma,
                                  struct nas_nvme_identity *out);

#endif
