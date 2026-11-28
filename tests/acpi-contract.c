#include "../kernel/platform/firmware_acpi.h"
#include <uacpi/acpi.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

static struct acpi_rsdp rsdp;
static unsigned char root[sizeof(struct acpi_sdt_hdr) + 16];
static unsigned char mcfg[sizeof(struct acpi_mcfg) + sizeof(struct acpi_mcfg_allocation)];
static struct acpi_madt madt;
static void checksum(void *p, size_t size, unsigned offset) {
    uint8_t *bytes = p;
    bytes[offset] = 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum += bytes[i];
    bytes[offset] = (uint8_t)-sum;
}
static const void *map(void *context, uint64_t address, size_t size) {
    (void)context;
    if (address == 0x2000 && size <= sizeof(root)) return root;
    if (address == 0x3000 && size <= sizeof(mcfg)) return mcfg;
    if (address == 0x4000 && size <= sizeof(madt)) return &madt;
    return NULL;
}
void nas_print(const char *message) { (void)message; }
int main(void) {
    memcpy(rsdp.signature, "RSD PTR ", 8);
    rsdp.revision = 2;
    rsdp.length = sizeof(rsdp);
    rsdp.xsdt_addr = 0x2000;
    checksum(&rsdp, 20, 8);
    checksum(&rsdp, sizeof(rsdp), 32);
    struct acpi_sdt_hdr hdr = {.length = sizeof(root)};
    memcpy(hdr.signature, "XSDT", 4);
    memcpy(root, &hdr, sizeof(hdr));
    uint64_t entries[] = {0x3000, 0x4000};
    memcpy(root + sizeof(hdr), entries, sizeof(entries));
    checksum(root, sizeof(root), 9);
    hdr.length = sizeof(mcfg);
    memcpy(hdr.signature, "MCFG", 4);
    memcpy(mcfg, &hdr, sizeof(hdr));
    struct acpi_mcfg_allocation alloc = {.address = 0xe0000000, .end_bus = 255};
    memcpy(mcfg + sizeof(struct acpi_mcfg), &alloc, sizeof(alloc));
    checksum(mcfg, sizeof(mcfg), 9);
    madt.hdr.length = sizeof(madt);
    memcpy(madt.hdr.signature, "APIC", 4);
    checksum(&madt, sizeof(madt), 9);
    struct nas_acpi_discovery out;
    assert(nas_acpi_probe(&rsdp, sizeof(rsdp), 0x1000, map, NULL, &out) == 0);
    assert(out.used_xsdt && out.madt_present && out.table_count == 2 &&
           out.mcfg_count == 1 && out.mcfg[0].ecam_base == 0xe0000000);
    mcfg[9]++;
    assert(nas_acpi_probe(&rsdp, sizeof(rsdp), 0x1000, map, NULL, &out) != 0);
    assert(!out.mcfg_count && !out.madt_present);
    mcfg[9]--;
    root[9]++;
    assert(nas_acpi_probe(&rsdp, sizeof(rsdp), 0x1000, map, NULL, &out) != 0);
    root[9]--;
    rsdp.checksum++;
    assert(nas_acpi_probe(&rsdp, sizeof(rsdp), 0x1000, map, NULL, &out) != 0);
    rsdp.checksum--;
    assert(nas_acpi_probe(&rsdp, sizeof(rsdp), 0x1000, map, NULL, &out) == 0);
    return 0;
}
