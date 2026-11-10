#include "../kernel/platform/pci_ecam.h"
#include "../kernel/drivers/core/pci_scan.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic, read-only ECAM host bridge. A bad address must fail without
 * leaking a read into an unrelated physical address or changing PCI config.
 * MCFG base denotes bus ZERO, not the first bus of a nonzero-start window:
 * see Linux Documentation/PCI/acpi-info.rst (MCFG and _CBA). */
#define MB UINT64_C(0x100000)
#define ECAM_BASE UINT64_C(0xe0000000)
#define SEGMENT 42U
#define FIRST_BUS 32U
#define LAST_BUS 33U

struct fake_ecam {
    uint64_t start;
    uint8_t *bytes;
    size_t size;
    unsigned reads;
    bool fault;
};

static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (i * 8));
}

static int read_mmio32(void *context, uint64_t physical, uint32_t *out) {
    struct fake_ecam *fake = context;
    if (fake->fault) return -EIO;
    if (!out || physical < fake->start ||
        physical - fake->start > fake->size - 4U || (physical & 3U))
        return -EFAULT;
    /* No BAR accesses for the NVMe endpoint while merely enumerating. */
    uint64_t offset = physical - fake->start;
    if (offset >= MB && offset < MB + UINT64_C(0x1000)) {
        uint64_t register_offset = offset & UINT64_C(0xfff);
        assert(register_offset < UINT64_C(0x10) ||
               register_offset > UINT64_C(0x24));
    }
    ++fake->reads;
    *out = load32(fake->bytes + (size_t)offset);
    return 0;
}

static void set_config(struct fake_ecam *fake, unsigned bus, unsigned slot,
                       unsigned function, unsigned offset, uint32_t value) {
    assert(bus >= FIRST_BUS && bus <= LAST_BUS);
    assert(slot < 32 && function < 8 && !(offset & 3U) && offset < 4096);
    uint64_t local = ((uint64_t)(bus - FIRST_BUS) << 20) |
                     ((uint64_t)slot << 15) |
                     ((uint64_t)function << 12) | offset;
    assert(local + 4 <= fake->size);
    store32(fake->bytes + (size_t)local, value);
}

static void endpoint(struct fake_ecam *fake, unsigned bus, unsigned slot,
                     unsigned function, uint16_t vendor, uint16_t device,
                     unsigned cls, unsigned sub, unsigned pi,
                     uint8_t header_type) {
    set_config(fake, bus, slot, function, 0,
               ((uint32_t)device << 16) | vendor);
    set_config(fake, bus, slot, function, 0x08,
               (cls << 24) | (sub << 16) | (pi << 8));
    set_config(fake, bus, slot, function, 0x0c,
               (uint32_t)header_type << 16);
    set_config(fake, bus, slot, function, 0x3c, 0);
}

static void test_address_translation(void) {
    struct nas_acpi_mcfg_window window = {
        .ecam_base = ECAM_BASE, .segment = SEGMENT,
        .bus_start = FIRST_BUS, .bus_end = LAST_BUS
    };
    uint64_t physical = 0;
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 0, 0, &physical) == 0);
    assert(physical == ECAM_BASE + (MB * FIRST_BUS));
    assert(nas_pci_ecam_address(&window, SEGMENT, LAST_BUS,
                                31, 7, 0xffc, &physical) == 0);
    assert(physical == ECAM_BASE + (MB * LAST_BUS) +
                       (UINT64_C(31) << 15) + (UINT64_C(7) << 12) +
                       UINT64_C(0xffc));
    /* Out-of-window and cross-segment requests must never become MMIO. */
    assert(nas_pci_ecam_address(&window, SEGMENT + 1U, FIRST_BUS,
                                0, 0, 0, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS - 1U,
                                0, 0, 0, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, LAST_BUS + 1U,
                                0, 0, 0, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                32, 0, 0, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 8, 0, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 0, 4096, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 0, 1, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(NULL, SEGMENT, FIRST_BUS,
                                0, 0, 0, &physical) == -EINVAL);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 0, 0, NULL) == -EINVAL);

    window.bus_start = LAST_BUS;
    window.bus_end = FIRST_BUS;
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 0, 0, &physical) == -EINVAL);
    window.bus_start = FIRST_BUS;
    window.bus_end = LAST_BUS;
    window.ecam_base = UINT64_MAX & ~(MB - 1U);
    assert(nas_pci_ecam_address(&window, SEGMENT, FIRST_BUS,
                                0, 0, 0, &physical) == -EOVERFLOW);
}

struct found {
    unsigned devices, bridges, nvme, multifunction;
};

static int visit(void *context, const struct nas_pci_function_info *info) {
    struct found *found = context;
    ++found->devices;
    if (info->bus == FIRST_BUS && info->slot == 1U) {
        assert(info->header_type == 1U && info->secondary_bus == LAST_BUS);
        ++found->bridges;
    }
    if (info->bus == LAST_BUS && info->slot == 0U) {
        assert(info->vendor_id == 0x1b36 && info->device_id == 0x0010);
        assert(info->class_code == 1U && info->subclass == 8U &&
               info->programming_interface == 2U);
        ++found->nvme;
    }
    if (info->bus == FIRST_BUS && info->slot == 2U && info->function == 1U)
        ++found->multifunction;
    return 0;
}

static void test_bridge_reader_and_faults(void) {
    struct fake_ecam fake = {0};
    fake.start = ECAM_BASE + MB * FIRST_BUS;
    fake.size = 2U * (size_t)MB;
    fake.bytes = malloc(fake.size);
    assert(fake.bytes);
    memset(fake.bytes, 0xff, fake.size);
    endpoint(&fake, FIRST_BUS, 0, 0, 0x8086, 0x1237, 6, 0, 0, 0);
    endpoint(&fake, FIRST_BUS, 1, 0, 0x8086, 0x244e, 6, 4, 0, 1);
    set_config(&fake, FIRST_BUS, 1, 0, 0x18,
               (FIRST_BUS | (LAST_BUS << 8) | (LAST_BUS << 16)));
    endpoint(&fake, LAST_BUS, 0, 0, 0x1b36, 0x0010, 1, 8, 2, 0);
    endpoint(&fake, FIRST_BUS, 2, 0, 0x1234, 0x0001, 3, 0, 0, 0x80);
    endpoint(&fake, FIRST_BUS, 2, 1, 0x10ec, 0x8139, 2, 0, 0, 0);

    struct nas_acpi_mcfg_window window = {
        .ecam_base = ECAM_BASE, .segment = SEGMENT,
        .bus_start = FIRST_BUS, .bus_end = LAST_BUS
    };
    struct nas_pci_ecam_host host = {
        .window = &window, .segment = SEGMENT,
        .context = &fake, .read32 = read_mmio32
    };
    const struct nas_pci_config_reader reader = {
        .context = &host, .read32 = nas_pci_ecam_scan_read32
    };
    struct found found = {0};
    struct nas_pci_scan_stats stats = {0};
    assert(nas_pci_scan_root(&reader, FIRST_BUS,
                             visit, &found, &stats) == 0);
    assert(stats.buses == 2U && stats.functions == 5U);
    assert(found.devices == 5U && found.bridges == 1U &&
           found.nvme == 1U && found.multifunction == 1U);
    assert(fake.reads > 64U && host.read_faults == 0U);

    unsigned reads_before = fake.reads;
    assert(nas_pci_ecam_scan_read32(&host, LAST_BUS + 1U,
                                    0, 0, 0) == UINT32_MAX);
    assert(fake.reads == reads_before && host.read_faults == 1U);
    fake.fault = true;
    assert(nas_pci_ecam_scan_read32(&host, FIRST_BUS,
                                    0, 0, 0) == UINT32_MAX);
    assert(host.read_faults == 2U);
    assert(fake.reads == reads_before);
    free(fake.bytes);
}

int main(void) {
    test_address_translation();
    test_bridge_reader_and_faults();
    puts("[H1 PCI ECAM HOST PASS] MCFG bus-zero base, MMIO bounds, hierarchy, read failures");
    return 0;
}
