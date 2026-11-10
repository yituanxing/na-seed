#ifndef NASEED_AHCI_READONLY_H
#define NASEED_AHCI_READONLY_H

#include "../port/dma.h"
#include <stdbool.h>
#include <stdint.h>

/* MMIO is supplied by a separately qualified, QEMU-only PCI adapter.
 * The core never discovers arbitrary hardware or writes PCI config space.
 * Offsets are relative to the AHCI BAR5 ABAR, not an NVMe BAR0 aperture. */
struct nas_ahci_io {
    void *context;
    uint32_t mmio_bytes;
    int (*read32)(void *context, uint32_t offset, uint32_t *value);
    int (*write32)(void *context, uint32_t offset, uint32_t value);
};

/* One SATA disk, one polling command slot, one 512-byte-sector bounce.
 * All four memory areas are individually leased as 4 KiB DMA pages, even
 * though the AHCI command list/FIS/table are smaller. The caller must
 * zero-initialize this session and keep it stable while commands execute.
 * A failed controller stop keeps every possibly-live DMA page pinned. */
struct nas_ahci_ro_session {
    const struct nas_ahci_io *io;
    const struct nas_dma_provider *dma;
    struct nas_dma_lease lease;
    struct nas_dma_page cmd_list;
    struct nas_dma_page received_fis;
    struct nas_dma_page cmd_table;
    struct nas_dma_page bounce;
    uint64_t sectors;
    uint32_t sector_bytes;
    uint32_t port_offset;
    uint8_t port;
    bool may_dma, ready, in_io;
};

/* Use only ATA IDENTIFY DEVICE (0xEC) and READ DMA EXT (0x25).
 * No SATA writes, FLUSH, SMART, partition management or hotplug. The caller
 * owns the port's PCI/BAR lifecycle and must quiesce this session before
 * removing bus mastering or releasing the MMIO mapping. */
int nas_ahci_ro_open(const struct nas_ahci_io *,
                     const struct nas_dma_provider *,
                     struct nas_ahci_ro_session *);
int nas_ahci_ro_read(struct nas_ahci_ro_session *, uint64_t lba,
                     uint32_t count, void *kernel_dst);
int nas_ahci_ro_close(struct nas_ahci_ro_session *);

#endif
