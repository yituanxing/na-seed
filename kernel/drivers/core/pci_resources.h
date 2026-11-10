#ifndef NASEED_PCI_RESOURCES_H
#define NASEED_PCI_RESOURCES_H

#include "pci_scan.h"
#include <stdbool.h>
#include <stdint.h>

/* Read-only PCI CONFIG metadata. A BAR register supplies an address and
 * encoding, never a proven aperture size or a safely mapped MMIO resource.
 * These snapshots MUST NOT be published as nas_bus_device resources before
 * an independent host bridge owner has qualified BAR size, IRQ and DMA. */
enum nas_pci_bar_kind {
    NAS_PCI_BAR_UNUSED = 0,
    NAS_PCI_BAR_IO,
    NAS_PCI_BAR_MEM32,
    NAS_PCI_BAR_MEM64
};

#define NAS_PCI_RESOURCE_BAR_LIMIT 6U
#define NAS_PCI_RESOURCE_CAP_LIMIT 32U
#define NAS_PCI_MMIO_CLAIM_LIMIT 8U

struct nas_pci_bar_desc {
    enum nas_pci_bar_kind kind;
    uint8_t index; /* Original BAR register number, not compact array slot. */
    bool prefetchable;
    uint64_t base;
};
struct nas_pci_cap_desc {
    uint8_t offset, id, next;
};
struct nas_pci_resources {
    uint16_t command, status;
    uint8_t header_type, bar_slots, bar_count, cap_count;
    struct nas_pci_bar_desc bars[NAS_PCI_RESOURCE_BAR_LIMIT];
    struct nas_pci_cap_desc caps[NAS_PCI_RESOURCE_CAP_LIMIT];
};

/* A platform-qualified *window* is independent of an unsized BAR snapshot.
 * The platform must establish that this is a safe host-bridge MMIO aperture
 * before constructing the owner. Claims only reserve a caller-requested
 * extent within that window: they DO NOT probe BAR size, map pages, assign
 * an IRQ, authorize DMA or prove that firmware has reserved the aperture. */
struct nas_pci_mmio_claim {
    uint64_t base, bytes;
    uint8_t bar_index;
    /* The claim cannot be reused once a page-table mapping may exist.
     * A physical MMIO map has no unmap operation in the current x86 port. */
    bool mapping_pinned;
};
struct nas_pci_mmio_owner {
    uint64_t window_base, window_bytes;
    unsigned held;
    struct nas_pci_mmio_claim claims[NAS_PCI_MMIO_CLAIM_LIMIT];
};

/* Fail closed on invalid, unaligned, overflowing, out-of-window or
 * overlapping claims. No PCI config I/O is performed. */
int nas_pci_mmio_claim_bar(struct nas_pci_mmio_owner *owner,
                           const struct nas_pci_resources *resources,
                           uint8_t bar_index,uint64_t required_bytes,
                           struct nas_pci_mmio_claim *out);
/* Must be called BEFORE attempting the first MMIO mapping, even if the
 * mapping callback can fail after installing one or more pages. An owner
 * permanently retains this reservation until a real unmap facility exists.
 * Idempotent for the same live claim. Never accesses device registers. */
int nas_pci_mmio_pin_mapping_attempt(struct nas_pci_mmio_owner *owner,
                                      const struct nas_pci_mmio_claim *claim);
/* Rejects releasing a pinned mapping. Resource release is only legal while
 * no MMIO page-table mapping attempt has occurred. */
int nas_pci_mmio_release(struct nas_pci_mmio_owner *owner,
                          const struct nas_pci_mmio_claim *claim);

/* No PCI config writes. Reads only Command/Status, BARs and conventional
 * capabilities (first 256 bytes). Rejects malformed reserved encodings,
 * incomplete 64-bit BARs and capability-pointer loops. The caller owns
 * reader fault checking: `nas_pci_ecam_host.read_faults` MUST remain zero.
 * On error, `out` remains completely zeroed. */
int nas_pci_resources_probe(const struct nas_pci_config_reader *reader,
                             const struct nas_pci_function_info *info,
                             struct nas_pci_resources *out);

#endif
