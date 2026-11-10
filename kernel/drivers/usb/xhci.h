#ifndef NASEED_USB_XHCI_H
#define NASEED_USB_XHCI_H

#include "../port/dma.h"
#include <stdbool.h>
#include <stdint.h>

/* QEMU-only xHCI MMIO aperture supplied by a separately validated PCI owner.
 * This controller core never discovers arbitrary PCI devices or enables BARs.
 * Offsets are relative to the controller's BAR0, not to xHC operational base.
 */
struct nas_xhci_io {
    void *context;
    uint32_t mmio_bytes;
    int (*read32)(void *context, uint32_t offset, uint32_t *value);
    int (*write32)(void *context, uint32_t offset, uint32_t value);
};

/* Seven physically separate, coherent, 4KiB DMA pages:
 * 0 DCBAA/ERST, 1 command ring, 2 event ring,
 * 3 input contexts, 4 output device context,
 * 5 EP0/bulk ring triplet, 6 control/bulk payload.
 * Every page is lease-pinned while DMA may be active. The eighth lease slot
 * remains unused; controllers requiring scratchpad buffers are rejected.
 * This is NOT a generic physical USB driver or an IOMMU security proof.
 */
#define NAS_XHCI_RO_DMA_PAGES 7U
struct nas_xhci_ro_session {
    const struct nas_xhci_io *io;
    const struct nas_dma_provider *dma;
    struct nas_dma_lease lease;
    struct nas_dma_page pages[NAS_XHCI_RO_DMA_PAGES];
    uint32_t op_base, rt_base, db_base;
    uint32_t portsc_offset;
    uint16_t context_bytes;
    uint16_t max_packet0;
    uint16_t max_packet_out;
    uint16_t max_packet_in;
    uint16_t cmd_pos, event_pos;
    uint16_t ep_pos[3];
    uint8_t cmd_cycle, event_cycle, ep_cycle[3];
    uint8_t slot_id;
    uint8_t port;
    uint8_t port_speed;
    uint8_t interface_number;
    uint8_t bulk_out_dci, bulk_in_dci;
    bool may_dma;
    bool ready;
    bool io_active;
};

/* Opens one xHCI root-port USB Mass Storage Bulk-Only Transport device
 * (interface class 08, subclass 06, protocol 50), with synchronous,
 * polling-only control + bulk transfers; no interrupts or hotplug.
 * Success guarantees both endpoints configured. Failure MUST be closed with
 * nas_xhci_ro_close() until a successful close releases all leased DMA.
 */
int nas_xhci_ro_open(const struct nas_xhci_io *io,
                     const struct nas_dma_provider *dma,
                     struct nas_xhci_ro_session *session);

/* Complete exactly 'bytes' or fail. Max one 4KiB payload per call; the
 * caller may split SCSI data phases into smaller transfers. The core may
 * return an error for a short or stalled USB transaction; no hidden retry.
 */
int nas_xhci_ro_bulk_out(struct nas_xhci_ro_session *,
                          const void *source, uint32_t bytes);
int nas_xhci_ro_bulk_in(struct nas_xhci_ro_session *,
                         void *destination, uint32_t bytes);

/* Stops the controller before freeing DMA, context and BAR ownership.
 * A failed controller stop retains all DMA leases (and cannot be treated
 * as safe PCI BAR release). Calling close again retries quiescence.
 */
int nas_xhci_ro_close(struct nas_xhci_ro_session *);

#endif
