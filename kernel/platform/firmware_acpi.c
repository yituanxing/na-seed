#include "firmware_acpi.h"
#include "../lib/string.h"
#include "../drivers/serial.h"
#include <uacpi/uacpi.h>
#include <uacpi/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/kernel_api.h>
#include <uacpi/internal/context.h>

/* H0 is a single-threaded, read-only boot probe. uACPI owns table discovery,
 * checksum verification and registration. NaSeed owns bounded mappings and
 * conversion of MCFG allocations into PCI resource windows. */
#define ACPI_RSDP_MAX_BYTES 4096U
#define ACPI_ROOT_MAX_BYTES 16384U
#define ACPI_TABLE_MAX_BYTES (1024U * 1024U)
#define ACPI_ROOT_ENTRY_LIMIT 256U
#define ACPI_PCI_BUS_BYTES UINT64_C(1048576)
static struct {
    nas_acpi_map_fn map;
    void *context;
    uint64_t rsdp_phys, root_phys;
    struct acpi_rsdp rsdp;
    unsigned entry_size, root_count;
    bool failed;
} probe;
static _Alignas(16) unsigned char table_storage[32768];

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out) {
    if (!probe.map) return UACPI_STATUS_NOT_FOUND;
    *out = probe.rsdp_phys;
    return UACPI_STATUS_OK;
}
void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    if (probe.map && addr == probe.rsdp_phys &&
        len == sizeof(probe.rsdp)) return &probe.rsdp;
    size_t limit = addr == probe.root_phys ?
                   ACPI_ROOT_MAX_BYTES : ACPI_TABLE_MAX_BYTES;
    if (!probe.map || !addr || len < sizeof(struct acpi_sdt_hdr) ||
        len > limit || addr > UINT64_MAX - len) goto fail;
    const void *ptr = probe.map(probe.context, addr, len);
    if (!ptr) goto fail;
    const struct acpi_sdt_hdr *hdr = ptr;
    if (hdr->length < sizeof(*hdr) || hdr->length > limit) goto fail;
    if (addr == probe.root_phys) {
        size_t payload = hdr->length - sizeof(*hdr);
        if (!payload || payload % probe.entry_size ||
            payload / probe.entry_size > ACPI_ROOT_ENTRY_LIMIT) goto fail;
        probe.root_count = (unsigned)(payload / probe.entry_size);
    }
    return (void *)ptr;
fail:
    probe.failed = true;
    return UACPI_MAP_FAILED;
}
void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    /* Caller owns permanent read-only HHDM mappings. */
    (void)addr; (void)len;
}
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *message) {
    (void)level;
    nas_print(message);
}

/* The bootloader's RSDP is a separate trust boundary: upstream early access
 * reads a full 36-byte RSDP and does not validate the RSDP checksum itself. */
static bool checksum_ok(const void *data, size_t bytes) {
    const uint8_t *p = data;
    uint8_t sum = 0;
    while (bytes--) sum = (uint8_t)(sum + *p++);
    return sum == 0;
}
static bool add_mcfg(struct nas_acpi_discovery *out,
                     const struct acpi_mcfg *table) {
    size_t length = table->hdr.length;
    if (length < sizeof(*table) ||
        (length - sizeof(*table)) % sizeof(struct acpi_mcfg_allocation))
        return false;
    size_t count = (length - sizeof(*table)) / sizeof(struct acpi_mcfg_allocation);
    if (count > NAS_ACPI_MCFG_WINDOW_LIMIT - out->mcfg_count) return false;
    for (size_t i = 0; i < count; ++i) {
        struct acpi_mcfg_allocation e = table->entries[i];
        struct nas_acpi_mcfg_window w = {
            .ecam_base = e.address, .segment = e.segment,
            .bus_start = e.start_bus, .bus_end = e.end_bus
        };
        uint64_t covered = ((uint64_t)w.bus_end + 1U) * ACPI_PCI_BUS_BYTES;
        if (w.bus_start > w.bus_end || !w.ecam_base ||
            (w.ecam_base & (ACPI_PCI_BUS_BYTES - 1U)) ||
            w.ecam_base > UINT64_MAX - covered) return false;
        for (unsigned j = 0; j < out->mcfg_count; ++j) {
            const struct nas_acpi_mcfg_window *p = &out->mcfg[j];
            if (p->segment == w.segment && p->bus_start <= w.bus_end &&
                w.bus_start <= p->bus_end) return false;
        }
        out->mcfg[out->mcfg_count++] = w;
    }
    return true;
}

int nas_acpi_probe(const void *rsdp, size_t available, uint64_t rsdp_phys,
                   nas_acpi_map_fn map, void *context,
                   struct nas_acpi_discovery *out) {
    if (!out) return -1;
    *out = (struct nas_acpi_discovery){0};
    if (!rsdp || !rsdp_phys || !map || available < 20U ||
        memcmp(rsdp, "RSD PTR ", 8U) || !checksum_ok(rsdp, 20U))
        return -1;
    memset(&probe, 0, sizeof(probe));
    memcpy(&probe.rsdp, rsdp, 20U);
    if (probe.rsdp.revision >= 2U) {
        if (available < sizeof(probe.rsdp)) return -1;
        memcpy(&probe.rsdp, rsdp, sizeof(probe.rsdp));
        size_t length = probe.rsdp.length;
        if (length < sizeof(probe.rsdp) || length > ACPI_RSDP_MAX_BYTES ||
            length > available || !checksum_ok(rsdp, length)) return -1;
    }
    struct nas_acpi_discovery found = {0};
    found.used_xsdt = probe.rsdp.revision >= 2U && probe.rsdp.xsdt_addr;
    probe.root_phys = found.used_xsdt ? probe.rsdp.xsdt_addr : probe.rsdp.rsdt_addr;
    if (!probe.root_phys || probe.root_phys == rsdp_phys) return -1;
    probe.entry_size = found.used_xsdt ? 8U : 4U;
    probe.rsdp_phys = rsdp_phys;
    probe.map = map;
    probe.context = context;
    /* 7.0.0 exposes proactive checking publicly, but early strict flags
     * require setting the pinned runtime context before table initialization. */
    g_uacpi_rt_ctx.flags = UACPI_FLAG_BAD_CSUM_FATAL |
        UACPI_FLAG_BAD_TBL_SIGNATURE_FATAL | UACPI_FLAG_PROACTIVE_TBL_CSUM;
    int result = -1;
    if (uacpi_setup_early_table_access(table_storage, sizeof(table_storage)) !=
        UACPI_STATUS_OK || probe.failed) goto done;

    /* uACPI also installs the DSDT referenced by FADT. Check that the
     * entire root inventory was installed: 7.0.0 can swallow an installation
     * error in its physical-table path, so setup success alone is insufficient. */
    size_t count = uacpi_table_count();
    struct acpi_fadt *fadt;
    size_t expected = probe.root_count;
    if (uacpi_table_fadt(&fadt) == UACPI_STATUS_OK && fadt->x_dsdt) ++expected;
    if (count != expected) goto done;
    found.table_count = probe.root_count;
    for (size_t i = 0; i < count; ++i) {
        uacpi_table table;
        if (uacpi_table_get_by_index(i, &table) != UACPI_STATUS_OK) goto done;
        bool ok = true;
        if (!memcmp(table.hdr->signature, "MCFG", 4))
            ok = add_mcfg(&found, table.ptr);
        else if (!memcmp(table.hdr->signature, "APIC", 4)) {
            ok = table.hdr->length >= sizeof(struct acpi_madt);
            found.madt_present = ok;
        }
        uacpi_table_unref(&table);
        if (!ok) goto done;
    }
    if (!probe.failed) { *out = found; result = 0; }
done:
    uacpi_state_reset();
    memset(&probe, 0, sizeof(probe));
    return result;
}
