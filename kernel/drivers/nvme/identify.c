#include "identify.h"
#include "../../errno.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NVM Express 1.x controller register layout; no namespaces or I/O queues. */
#define NVME_CAP           0x00U
#define NVME_VS            0x08U
#define NVME_CC            0x14U
#define NVME_CSTS          0x1cU
#define NVME_AQA           0x24U
#define NVME_ASQ           0x28U
#define NVME_ACQ           0x30U
#define NVME_SQ0_TDBL      0x1000U
#define NVME_CC_EN         1U
#define NVME_CSTS_RDY      1U
#define NVME_CSTS_CFS      2U
#define NVME_ADMIN_IDENTIFY 6U
#define NVME_IDENTIFY_CNS_CONTROLLER 1U
#define NVME_ADMIN_DEPTH   2U
#define NVME_POLL_LIMIT    1000000U

static int mmio_read(const struct nas_nvme_io *io, uint32_t offset,
                     uint32_t *value) {
    if (!value || (offset & 3U) || offset > io->mmio_bytes ||
        io->mmio_bytes - offset < 4U) return -EINVAL;
    return io->read32(io->context, offset, value);
}

static int mmio_write(const struct nas_nvme_io *io, uint32_t offset,
                      uint32_t value) {
    if ((offset & 3U) || offset > io->mmio_bytes ||
        io->mmio_bytes - offset < 4U) return -EINVAL;
    return io->write32(io->context, offset, value);
}

static int poll_ready(const struct nas_nvme_io *io, bool wanted) {
    for (unsigned i = 0; i < NVME_POLL_LIMIT; ++i) {
        uint32_t status;
        int rc = mmio_read(io, NVME_CSTS, &status);
        if (rc) return rc;
        if (status & NVME_CSTS_CFS) return -EIO;
        if (((status & NVME_CSTS_RDY) != 0U) == wanted) return 0;
    }
    return -ETIMEDOUT;
}

static int write_dma_address(const struct nas_nvme_io *io,
                             uint32_t register_offset, uint64_t address) {
    int rc = mmio_write(io, register_offset, (uint32_t)address);
    if (!rc) rc = mmio_write(io, register_offset + 4U,
                             (uint32_t)(address >> 32));
    return rc;
}

static void copy_identify_string(char *out, size_t dst_size,
                                 const uint8_t *data, size_t count) {
    size_t n = count < dst_size - 1U ? count : dst_size - 1U;
    while (n && (data[n - 1] == ' ' || data[n - 1] == '\0')) --n;
    for (size_t i = 0; i < n; ++i) out[i] = (char)data[i];
    out[n] = '\0';
}

static uint16_t from_le16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
static uint32_t from_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void to_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int nas_nvme_identify_controller(const struct nas_nvme_io *io,
                                  const struct nas_dma_provider *dma,
                                  struct nas_nvme_identity *out) {
    if (!io || !io->read32 || !io->write32 || !dma || !out ||
        io->mmio_bytes < NVME_SQ0_TDBL + 8U) return -EINVAL;
    *out = (struct nas_nvme_identity){0};

    uint32_t lo, hi, version, initial_cc;
    int rc = mmio_read(io, NVME_CAP, &lo);
    if (!rc) rc = mmio_read(io, NVME_CAP + 4U, &hi);
    if (rc) return rc;
    const uint64_t cap = (uint64_t)lo | ((uint64_t)hi << 32);
    if ((cap & UINT32_C(0xffff)) < NVME_ADMIN_DEPTH - 1U ||
        !(cap & (UINT64_C(1) << 37)) ||
        ((cap >> 48) & 15U) != 0U) return -EOPNOTSUPP;
    const uint32_t stride = (uint32_t)((cap >> 32) & 15U);
    if (stride > 16U) return -EOPNOTSUPP;
    const uint32_t cq0_db = NVME_SQ0_TDBL + (4U << stride);
    if (cq0_db > io->mmio_bytes || io->mmio_bytes - cq0_db < 4U)
        return -EOPNOTSUPP;
    rc = mmio_read(io, NVME_VS, &version);
    if (!rc) rc = mmio_read(io, NVME_CC, &initial_cc);
    if (rc) return rc;
    if ((version >> 16) == 0U || version == UINT32_MAX) return -EIO;

    /* QEMU fixture owns this controller exclusively; reset prior state. */
    if (initial_cc & NVME_CC_EN) {
        rc = mmio_write(io, NVME_CC, initial_cc & ~NVME_CC_EN);
        if (rc) return rc;
    }
    rc = poll_ready(io, false);
    if (rc) return rc;

    struct nas_dma_page sq = {0}, cq = {0}, identify = {0};
    bool controller_may_dma = false;
    rc = nas_dma_alloc_page(dma, &sq);
    if (rc) return rc;
    rc = nas_dma_alloc_page(dma, &cq);
    if (rc) goto release;
    rc = nas_dma_alloc_page(dma, &identify);
    if (rc) goto release;
    if ((sq.bus_address & 4095U) || (cq.bus_address & 4095U) ||
        (identify.bus_address & 4095U)) { rc = -EINVAL; goto release; }

    /* AQA CQ size in bits 27:16, SQ size in bits 11:0 (both zero-based). */
    const uint32_t aqa = (NVME_ADMIN_DEPTH - 1U) |
                         ((NVME_ADMIN_DEPTH - 1U) << 16);
    rc = mmio_write(io, NVME_AQA, aqa);
    if (!rc) rc = write_dma_address(io, NVME_ASQ, sq.bus_address);
    if (!rc) rc = write_dma_address(io, NVME_ACQ, cq.bus_address);
    if (rc) goto release;

    /* IOSQES=6 (64B), IOCQES=4 (16B), 4KiB pages, CSS=NVM, AMS=RR. */
    const uint32_t cc = NVME_CC_EN | (6U << 16) | (4U << 20);
    controller_may_dma = true; /* CC write may apply despite callback error */
    rc = mmio_write(io, NVME_CC, cc);
    if (rc) goto quiesce;
    rc = poll_ready(io, true);
    if (rc) goto quiesce;

    uint8_t *s = (uint8_t *)sq.cpu;
    to_le32(s, NVME_ADMIN_IDENTIFY | (1U << 16)); /* CID=1 */
    to_le32(s + 24U, (uint32_t)identify.bus_address); /* PRP1 */
    to_le32(s + 28U, (uint32_t)(identify.bus_address >> 32));
    to_le32(s + 40U, NVME_IDENTIFY_CNS_CONTROLLER); /* CDW10.CNS */
    rc = nas_dma_sync(dma, &sq, 0U, 64U, NAS_DMA_TO_DEVICE);
    if (rc) goto quiesce;
    rc = nas_dma_sync(dma, &cq, 0U, 16U, NAS_DMA_FROM_DEVICE);
    if (rc) goto quiesce;
    rc = mmio_write(io, NVME_SQ0_TDBL, 1U);
    if (rc) goto quiesce;

    bool complete = false;
    for (unsigned poll = 0; poll < NVME_POLL_LIMIT; ++poll) {
        rc = nas_dma_sync(dma, &cq, 0U, 16U, NAS_DMA_FROM_DEVICE);
        if (rc) break;
        const uint8_t *c = (const uint8_t *)cq.cpu;
        const uint32_t dw3 = from_le32(c + 12U);
        if (!(dw3 & UINT32_C(0x10000))) continue; /* CQ phase starts at 1 */
        const uint32_t dw2 = from_le32(c + 8U);
        if ((dw3 & UINT32_C(0xffff)) != 1U ||
            (dw2 >> 16) != 0U || (dw3 >> 17) != 0U) {
            rc = -EIO;
            break;
        }
        complete = true;
        break;
    }
    if (!rc && !complete) rc = -ETIMEDOUT;
    if (rc) goto quiesce;
    rc = mmio_write(io, cq0_db, 1U);
    if (rc) goto quiesce;
    rc = nas_dma_sync(dma, &identify, 0U, NAS_DMA_PAGE_BYTES,
                      NAS_DMA_FROM_DEVICE);
    if (rc) goto quiesce;
    const uint8_t *id = (const uint8_t *)identify.cpu;
    out->vendor_id = from_le16(id);
    out->namespace_count = from_le32(id + 516U);
    out->version = version;
    copy_identify_string(out->serial, sizeof(out->serial), id + 4U, 20U);
    copy_identify_string(out->model, sizeof(out->model), id + 24U, 40U);

quiesce:
    if (controller_may_dma) {
        /* Fail closed. A failed MMIO disable or never-cleared CSTS.RDY
         * leaves DMA pages pinned: freeing them risks a use-after-free. */
        int stop = mmio_write(io, NVME_CC, cc & ~NVME_CC_EN);
        if (!stop) stop = poll_ready(io, false);
        if (stop) {
            *out = (struct nas_nvme_identity){0};
            return rc ? rc : stop;
        }
    }
release:
    if (identify.cpu && nas_dma_free_page(dma, &identify) && !rc) rc = -EIO;
    if (cq.cpu && nas_dma_free_page(dma, &cq) && !rc) rc = -EIO;
    if (sq.cpu && nas_dma_free_page(dma, &sq) && !rc) rc = -EIO;
    if (rc) *out = (struct nas_nvme_identity){0};
    return rc;
}
