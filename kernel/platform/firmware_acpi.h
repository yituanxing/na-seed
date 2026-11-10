#ifndef NASEED_FIRMWARE_ACPI_H
#define NASEED_FIRMWARE_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* H0 is read-only firmware discovery. The caller owns all physical mappings
 * and must reject ranges outside its validated Limine memory map. The parser
 * never maps MMIO, writes PCI configuration space or binds a device driver.
 */
typedef const void *(*nas_acpi_map_fn)(void *context, uint64_t physical,
                                      size_t bytes);

#define NAS_ACPI_MCFG_WINDOW_LIMIT 16U
struct nas_acpi_mcfg_window {
    uint64_t ecam_base;
    uint16_t segment;
    uint8_t bus_start, bus_end;
};

struct nas_acpi_discovery {
    bool used_xsdt;
    bool madt_present;
    unsigned table_count;
    unsigned mcfg_count;
    struct nas_acpi_mcfg_window mcfg[NAS_ACPI_MCFG_WINDOW_LIMIT];
};

/* `rsdp_available` is the readable extent of the Limine-provided RSDP
 * pointer; it must be established by the caller, not guessed by this parser.
 * Every ACPI physical table access is mediated by `map`, which must return
 * one contiguous, readable range for the *full* requested byte count.
 * Return 0 only after all requested table/checksum/resource validation.
 * A nonzero result leaves `out` entirely zeroed, without partial discovery.
 */
int nas_acpi_probe(const void *rsdp, size_t rsdp_available,
                   nas_acpi_map_fn map, void *context,
                   struct nas_acpi_discovery *out);

#endif
