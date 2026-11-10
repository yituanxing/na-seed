#ifndef NASEED_NVME_READONLY_H
#define NASEED_NVME_READONLY_H

#include "identify.h"
#include <stdbool.h>
#include <stdint.h>

/* The H4 protocol core owns the two admin/IO queues and one bounce page.
 * This is NOT a physical-NVMe driver or a second block/VFS registry: QEMU's
 * qualified adapter supplies MMIO/DMA, then nas_port_block supplies ABI.
 * Session instances must be zero-initialized, stable and single-threaded.
 */
#define NAS_NVME_RO_QUEUE_DEPTH 4U
struct nas_nvme_ro_queue {
    struct nas_dma_page sq, cq;
    uint16_t cid_next;
    uint8_t sq_tail, cq_head, cq_phase, qid;
};
struct nas_nvme_ro_session {
    const struct nas_nvme_io *io;
    const struct nas_dma_provider *dma;
    struct nas_nvme_ro_queue admin, data;
    struct nas_dma_page bounce;
    uint64_t sectors;
    uint32_t sector_bytes;
    uint8_t doorbell_stride;
    bool may_dma, ready;
};

/* QEMU disposable-namespace-only: identify NSID=1, validate its selected
 * LBA format and create one polling I/O SQ/CQ. One LBA per read and only
 * NVMe READ opcode 0x02; no Write, Format, Flush or namespace management.
 * Failed controller disable leaves all DMA pages pinned for safety. */
int nas_nvme_ro_open(const struct nas_nvme_io *,
                     const struct nas_dma_provider *,
                     struct nas_nvme_ro_session *);
int nas_nvme_ro_read(struct nas_nvme_ro_session *, uint64_t lba,
                     uint32_t count, void *kernel_dst);
int nas_nvme_ro_close(struct nas_nvme_ro_session *);

#endif
