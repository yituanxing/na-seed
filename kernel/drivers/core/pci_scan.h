#ifndef NASEED_PCI_SCAN_H
#define NASEED_PCI_SCAN_H

#include <stdint.h>

/* Read-only PCI configuration discovery. The host bridge owns the actual
 * config-space access (CF8/CFC, ECAM, or a test fixture); this walker NEVER
 * writes configuration registers, reads/sizes BARs, enables bus mastering or
 * binds drivers. A later platform resource owner may publish accepted devices to
 * the existing nas_bus_device registry. */
#define NAS_PCI_SCAN_MAX_BUSES 256U

struct nas_pci_config_reader {
    void *context;
    uint32_t (*read32)(void *context, uint8_t bus, uint8_t slot,
                       uint8_t function, uint16_t offset);
};

struct nas_pci_function_info {
    uint8_t bus, slot, function;
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, programming_interface, revision;
    uint8_t header_type; /* low seven bits: 0 endpoint, 1 PCI bridge */
    uint8_t irq_line, irq_pin; /* firmware values, NOT an IRQ route */
    uint8_t secondary_bus, subordinate_bus; /* bridge only */
};

struct nas_pci_scan_stats {
    uint32_t buses;
    uint32_t functions;
    uint32_t bridges_ignored; /* malformed or already queued bus range */
};

/* Return zero to continue, negative errno to stop. Visitor must only inspect
 * this temporary descriptor; it must copy anything needed after returning. */
typedef int (*nas_pci_function_visit)(void *context,
                                      const struct nas_pci_function_info *);

/* Scan exactly the reachable hierarchy from one firmware-provided root bus.
 * Bridges are traversed once; loops and malformed secondary/subordinate bus
 * numbers are rejected. The caller is responsible for validating the host
 * bridge/segment and for ensuring config reads are safe on its platform. */
int nas_pci_scan_root(const struct nas_pci_config_reader *reader,
                      uint8_t root_bus, nas_pci_function_visit visit,
                      void *visitor_context, struct nas_pci_scan_stats *stats);

#endif
