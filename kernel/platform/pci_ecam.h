#ifndef NASEED_PCI_ECAM_H
#define NASEED_PCI_ECAM_H

#include <stdint.h>
#include "firmware_acpi.h"

/* ECAM is a platform resource, not a PCI driver. The MCFG base addresses
 * bus ZERO of a segment, even when bus_start is nonzero. A platform must
 * separately establish that the physical MMIO page is accessible through a
 * supervisor-only, read-only, uncached mapping before servicing read32. */
int nas_pci_ecam_address(const struct nas_acpi_mcfg_window *window,
                         uint16_t segment, uint8_t bus, uint8_t slot,
                         uint8_t function, uint16_t offset,
                         uint64_t *physical);

struct nas_pci_ecam_host {
    const struct nas_acpi_mcfg_window *window;
    uint16_t segment;
    void *context;
    /* Return zero only after a complete aligned DWORD MMIO read.
     * Never issue a PCI configuration WRITE, allocate BARs, or bind drivers. */
    int (*read32)(void *context, uint64_t physical, uint32_t *value);
    unsigned read_faults; /* Caller MUST reject a scan with nonzero faults. */
};

/* Adapter to `nas_pci_config_reader`; invalid or inaccessible DWORDs read
 * as all-ones but increment read_faults instead of silently hiding them. */
uint32_t nas_pci_ecam_scan_read32(void *context, uint8_t bus, uint8_t slot,
                                  uint8_t function, uint16_t offset);

#endif
