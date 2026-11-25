#ifndef NASEED_NVME_CONTROLLER_INTERNAL_H
#define NASEED_NVME_CONTROLLER_INTERNAL_H
#include "identify.h"
#include "../port/mmio.h"
#include "../../lib/endian.h"
#include "../../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* Shared controller register and byte-order primitives. Queue ownership,
 * completion validation and DMA lifetime policy stay with each caller. */
#define NVME_CAP           0x00U
#define NVME_CC            0x14U
#define NVME_CSTS          0x1cU
#define NVME_AQA           0x24U
#define NVME_ASQ           0x28U
#define NVME_ACQ           0x30U
#define NVME_CC_EN         1U
#define NVME_CSTS_RDY      1U
#define NVME_CSTS_CFS      2U
#define NVME_POLL_LIMIT    1000000U

static inline int nas_nvme_mmio_read(const struct nas_nvme_io *io, uint32_t offset,
                     uint32_t *value) {
    if (!io || !io->read32 || !value || !nas_mmio_u32_valid(io->mmio_bytes,offset)) return -EINVAL;
    return io->read32(io->context, offset, value);
}

static inline int nas_nvme_mmio_write(const struct nas_nvme_io *io, uint32_t offset,
                      uint32_t value) {
    if (!io || !io->write32 || !nas_mmio_u32_valid(io->mmio_bytes,offset)) return -EINVAL;
    return io->write32(io->context, offset, value);
}

static inline int nas_nvme_poll_ready(const struct nas_nvme_io *io, bool wanted) {
    for (unsigned i = 0; i < NVME_POLL_LIMIT; ++i) {
        uint32_t status = 0;
        int rc = nas_nvme_mmio_read(io, NVME_CSTS, &status);
        if (rc) return rc;
        if (status & NVME_CSTS_CFS) return -EIO;
        if (((status & NVME_CSTS_RDY) != 0U) == wanted) return 0;
    }
    return -ETIMEDOUT;
}

static inline int nas_nvme_write_dma_address(const struct nas_nvme_io *io,
                             uint32_t register_offset, uint64_t address) {
    int rc = nas_nvme_mmio_write(io, register_offset, (uint32_t)address);
    if (!rc) rc = nas_nvme_mmio_write(io, register_offset + 4U,
                             (uint32_t)(address >> 32));
    return rc;
}


#endif
