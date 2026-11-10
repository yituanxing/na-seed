#include "../kernel/drivers/core/pci_scan.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* All test configuration space is immutable from the production scanner's
 * viewpoint. This includes bridge configuration and BAR assignments. */
struct fake_function {
    uint8_t bus, slot, function;
    uint32_t config[64];
};
struct fake_host {
    struct fake_function functions[20];
    unsigned used;
    unsigned reads;
};

static struct fake_function *add_function(struct fake_host *host,
                                         uint8_t bus, uint8_t slot,
                                         uint8_t function,
                                         uint16_t vendor, uint16_t device,
                                         uint8_t cls, uint8_t sub,
                                         uint8_t pi, uint8_t header) {
    assert(host->used < 20U);
    struct fake_function *f = &host->functions[host->used++];
    f->bus = bus;
    f->slot = slot;
    f->function = function;
    f->config[0] = ((uint32_t)device << 16) | vendor;
    f->config[2] = ((uint32_t)cls << 24) | ((uint32_t)sub << 16) |
                   ((uint32_t)pi << 8) | 0x12U;
    f->config[3] = (uint32_t)header << 16;
    return f;
}

static uint32_t fake_read32(void *context, uint8_t bus, uint8_t slot,
                            uint8_t function, uint16_t offset) {
    struct fake_host *host = context;
    assert(slot < 32U && function < 8U);
    assert(!(offset & 3U) && offset < 256U);
    /* Hardware-inventory probing must never even read an endpoint BAR. */
    assert(!(bus == 1U && slot == 0U && function == 0U &&
             offset >= 0x10U && offset <= 0x24U));
    assert(++host->reads < 10000U); /* bridges may not cause infinite loops */
    for (unsigned i = 0; i < host->used; ++i) {
        const struct fake_function *f = &host->functions[i];
        if (f->bus == bus && f->slot == slot && f->function == function)
            return f->config[offset / 4U];
    }
    return UINT32_MAX; /* PCI no-function config read */
}

struct visitor_state {
    unsigned total;
    unsigned nvme;
    unsigned second_function;
    unsigned ghost_function;
    unsigned bridge;
    unsigned error_on_nvme;
};

static int visitor(void *context, const struct nas_pci_function_info *info) {
    struct visitor_state *state = context;
    ++state->total;
    assert(info->vendor_id != UINT16_MAX);
    if (info->bus == 1 && info->slot == 0) {
        assert(info->vendor_id == 0x1b36U && info->device_id == 0x0010U);
        assert(info->class_code == 1U && info->subclass == 8U);
        assert(info->programming_interface == 2U);
        assert(info->irq_pin == 1U);
        ++state->nvme;
        if (state->error_on_nvme) return -EIO;
    }
    if (info->bus == 0 && info->slot == 3 && info->function == 1)
        ++state->second_function;
    if (info->bus == 0 && info->slot == 4 && info->function == 1)
        ++state->ghost_function;
    if (info->header_type == 1U && info->class_code == 6U &&
        info->subclass == 4U) ++state->bridge;
    return 0;
}

static void topology_test(void) {
    struct fake_host host = {0};
    add_function(&host, 0, 0, 0, 0x8086, 0x1237, 0x06, 0x00, 0, 0);
    struct fake_function *bridge = add_function(&host, 0, 2, 0, 0x8086,
                                                 0x244e, 6, 4, 0, 1);
    bridge->config[0x18U / 4U] = UINT32_C(0x00010100); /* 0 -> 1..1 */
    struct fake_function *nvme = add_function(&host, 1, 0, 0, 0x1b36,
                                               0x0010, 1, 8, 2, 0);
    nvme->config[0x10U / 4U] = UINT32_C(0x80000004);
    nvme->config[0x3cU / 4U] = UINT32_C(0x0000010b);
    struct fake_function *loop = add_function(&host, 1, 1, 0, 0x8086,
                                               0x2222, 6, 4, 0, 1);
    loop->config[0x18U / 4U] = UINT32_C(0x00010001); /* invalid bus 0 */
    add_function(&host, 0, 3, 0, 0x1234, 0x1111, 3, 0, 0, 0x80);
    add_function(&host, 0, 3, 1, 0x10ec, 0x8139, 2, 0, 0, 0);
    add_function(&host, 0, 4, 0, 0x1234, 0x2222, 3, 0, 0, 0);
    add_function(&host, 0, 4, 1, 0x1234, 0x3333, 3, 0, 0, 0);
    struct fake_function *duplicate = add_function(&host, 0, 5, 0,
                                                    0x8086, 0x5555,
                                                    6, 4, 0, 1);
    duplicate->config[0x18U / 4U] = UINT32_C(0x00010100);
    struct fake_function *bad_primary = add_function(&host, 0, 6, 0,
                                                      0x8086, 0x6666,
                                                      6, 4, 0, 1);
    bad_primary->config[0x18U / 4U] = UINT32_C(0x00020201);
    struct fake_function *bad_range = add_function(&host, 0, 7, 0,
                                                    0x8086, 0x7777,
                                                    6, 4, 0, 1);
    bad_range->config[0x18U / 4U] = UINT32_C(0x00010200);

    const struct nas_pci_config_reader reader = {&host, fake_read32};
    struct nas_pci_scan_stats stats = {0};
    struct visitor_state state = {0};
    assert(nas_pci_scan_root(&reader, 0, visitor, &state, &stats) == 0);
    assert(stats.buses == 2U);
    assert(stats.functions == 10U && state.total == 10U);
    assert(stats.bridges_ignored == 4U);
    assert(state.nvme == 1U && state.second_function == 1U);
    assert(state.ghost_function == 0U && state.bridge == 5U);

    /* Reader error is propagated; partial stats remain inspectable. */
    struct visitor_state failure = {.error_on_nvme = 1};
    assert(nas_pci_scan_root(&reader, 0, visitor, &failure, &stats) == -EIO);
    assert(failure.nvme == 1U && stats.buses == 2U);
    assert(host.reads < 10000U);
}

static void root_and_error_test(void) {
    struct fake_host host = {0};
    struct nas_pci_config_reader reader = {&host, fake_read32};
    struct visitor_state visitor_state = {0};
    struct nas_pci_scan_stats stats = {0};
    assert(nas_pci_scan_root(NULL, 0, visitor, &visitor_state, &stats) == -EINVAL);
    assert(nas_pci_scan_root(&reader, 0, NULL, &visitor_state, &stats) == -EINVAL);
    assert(nas_pci_scan_root(&reader, 0, visitor, &visitor_state, NULL) == -EINVAL);
    assert(nas_pci_scan_root(&reader, 200, visitor, &visitor_state, &stats) == 0);
    assert(stats.buses == 1U && stats.functions == 0U);
    assert(host.reads == 32U);
}

int main(void) {
    topology_test();
    root_and_error_test();
    puts("[H0 PCI READ-ONLY DISCOVERY HOST PASS]");
    return 0;
}
