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
/* Last attempted controller phase, retained after a failed open for a
 * QEMU-serial fail-only diagnostic. This is NOT a success milestone.
 */
enum nas_xhci_ro_debug_stage {
    NAS_XHCI_STAGE_IDLE,
    NAS_XHCI_STAGE_DMA,
    NAS_XHCI_STAGE_CAPS,
    NAS_XHCI_STAGE_HALT,
    NAS_XHCI_STAGE_RESET,
    NAS_XHCI_STAGE_RING_CONFIG,
    NAS_XHCI_STAGE_RUN,
    NAS_XHCI_STAGE_PORT,
    NAS_XHCI_STAGE_ENABLE_SLOT,
    NAS_XHCI_STAGE_ADDRESS_DEVICE,
    NAS_XHCI_STAGE_GET_DEVICE_DESC,
    NAS_XHCI_STAGE_GET_CONFIG_HEADER,
    NAS_XHCI_STAGE_GET_CONFIG_FULL,
    NAS_XHCI_STAGE_SET_CONFIG,
    NAS_XHCI_STAGE_CONFIGURE_EP,
    NAS_XHCI_STAGE_USB_MSC_READY,
    NAS_XHCI_STAGE_BULK,
    NAS_XHCI_STAGE_HID_GET_DEVICE_DESC,
    NAS_XHCI_STAGE_HID_GET_CONFIG_HEADER,
    NAS_XHCI_STAGE_HID_GET_CONFIG_FULL,
    NAS_XHCI_STAGE_HID_SET_CONFIG,
    NAS_XHCI_STAGE_HID_SET_PROTOCOL,
    NAS_XHCI_STAGE_HID_CONFIGURE_EP,
    NAS_XHCI_STAGE_HID_ARM,
    NAS_XHCI_STAGE_HID_READY,
    NAS_XHCI_STAGE_HID_POLL
};
struct nas_xhci_ro_session {
    const struct nas_xhci_io *io;
    const struct nas_dma_provider *dma;
    struct nas_dma_lease lease;
    struct nas_dma_page pages[NAS_XHCI_RO_DMA_PAGES];
    uint32_t op_base, rt_base, db_base;
    uint32_t portsc_offset;
    uint32_t debug_stage;
    uint32_t debug_completion_code;
    uint32_t debug_event_type;
    uint32_t debug_portsc;
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
    uint8_t hid_in_dci;
    uint8_t hid_interval;
    uint16_t hid_packet;
    uint64_t hid_trb_bus;
    bool may_dma;
    bool ready;
    bool io_active;
    bool hid_mode;
    bool hid_pending;
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

/* H12 QEMU-only HID Boot Keyboard mode. This is a separate, HID-only
 * controller lifetime, never concurrent with the H11 mass-storage mode.
 * The caller must zero-initialize the same leased session structure and use
 * the same independently qualified xHCI MMIO and QEMU-coherent DMA provider.
 * Requires interface class/subclass/protocol 03/01/01, one Interrupt-IN
 * endpoint and the standard eight-byte USB Boot Keyboard input report.
 * Open arms exactly one Interrupt-IN TRB before returning.
 */
int nas_xhci_hid_open(const struct nas_xhci_io *,
                      const struct nas_dma_provider *,
                      struct nas_xhci_ro_session *);

/* Nonblocking, polling-only completion. 0 means one real complete eight-byte
 * report was copied and the next Interrupt-IN TRB rearmed. -EAGAIN means no
 * report is ready: output is unmodified and NO additional TRB is enqueued.
 * Other negative errno means an actual error, no fabricated report or
 * hidden reset/retry. Caller must close even after a failed open/poll.
 */
int nas_xhci_hid_poll(struct nas_xhci_ro_session *, uint8_t report[8]);

/* Alias of the same bounded HCHalted-first DMA release as H11.
 * On failure, PCI BAR and all DMA memory remain pinned for retry.
 */
int nas_xhci_hid_close(struct nas_xhci_ro_session *);

#endif
