#include "pci_ecam.h"
#include "../errno.h"
#include <stddef.h>
#include <limits.h>

#define NAS_ECAM_BUS_BYTES (UINT64_C(1) << 20)

int nas_pci_ecam_address(const struct nas_acpi_mcfg_window *window,
                         uint16_t segment, uint8_t bus, uint8_t slot,
                         uint8_t function, uint16_t offset,
                         uint64_t *physical) {
    if (!window || !physical || !window->ecam_base ||
        (window->ecam_base & (NAS_ECAM_BUS_BYTES-1U)) ||
        window->bus_start > window->bus_end ||
        window->segment != segment ||
        bus < window->bus_start || bus > window->bus_end ||
        slot >= 32U || function >= 8U || offset > 4092U || (offset & 3U))
        return -EINVAL;
    /* Bus offset is absolute: do NOT subtract window->bus_start. */
    uint64_t displacement = ((uint64_t)bus << 20) |
                            ((uint64_t)slot << 15) |
                            ((uint64_t)function << 12) |
                            (uint64_t)offset;
    if (window->ecam_base > UINT64_MAX-displacement) return -EOVERFLOW;
    *physical = window->ecam_base + displacement;
    return 0;
}

uint32_t nas_pci_ecam_scan_read32(void *context, uint8_t bus, uint8_t slot,
                                  uint8_t function, uint16_t offset) {
    struct nas_pci_ecam_host *host = context;
    uint64_t physical = 0;
    uint32_t value = UINT32_MAX;
    if (!host) return UINT32_MAX;
    if (!host->read32 ||
        nas_pci_ecam_address(host->window,host->segment,bus,slot,function,
                             offset,&physical) ||
        host->read32(host->context,physical,&value)) {
        if (host->read_faults != UINT_MAX) ++host->read_faults;
        return UINT32_MAX;
    }
    return value;
}
