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

/* No PCI config writes. Reads only Command/Status, BARs and conventional
 * capabilities (first 256 bytes). Rejects malformed reserved encodings,
 * incomplete 64-bit BARs and capability-pointer loops. The caller owns
 * reader fault checking: `nas_pci_ecam_host.read_faults` MUST remain zero.
 * On error, `out` remains completely zeroed. */
int nas_pci_resources_probe(const struct nas_pci_config_reader *reader,
                             const struct nas_pci_function_info *info,
                             struct nas_pci_resources *out);

#endif
