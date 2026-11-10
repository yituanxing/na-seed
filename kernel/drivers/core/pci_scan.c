#include "pci_scan.h"
#include "../../errno.h"
#include <stdbool.h>
#include <stddef.h>

static uint32_t read32(const struct nas_pci_config_reader *r, uint8_t bus,
                       uint8_t slot, uint8_t function, uint16_t offset) {
    return r->read32(r->context, bus, slot, function, offset);
}

static int scan_function(const struct nas_pci_config_reader *reader,
                         uint8_t bus, uint8_t slot, uint8_t function,
                         uint32_t identity, nas_pci_function_visit visit,
                         void *visitor_context, struct nas_pci_scan_stats *stats,
                         uint8_t *seen, uint8_t *queue,
                         unsigned *tail) {
    struct nas_pci_function_info info = {0};
    info.bus = bus;
    info.slot = slot;
    info.function = function;
    info.vendor_id = (uint16_t)identity;
    info.device_id = (uint16_t)(identity >> 16);
    uint32_t class_reg = read32(reader, bus, slot, function, 0x08U);
    info.revision = (uint8_t)class_reg;
    info.programming_interface = (uint8_t)(class_reg >> 8);
    info.subclass = (uint8_t)(class_reg >> 16);
    info.class_code = (uint8_t)(class_reg >> 24);
    uint32_t header = read32(reader, bus, slot, function, 0x0cU);
    info.header_type = (uint8_t)((header >> 16) & 0x7fU);
    uint32_t interrupts = read32(reader, bus, slot, function, 0x3cU);
    info.irq_line = (uint8_t)interrupts;
    info.irq_pin = (uint8_t)(interrupts >> 8);
    if (info.header_type == 1U && info.class_code == 0x06U &&
        info.subclass == 0x04U) {
        uint32_t buses = read32(reader, bus, slot, function, 0x18U);
        uint8_t primary = (uint8_t)buses;
        uint8_t secondary = (uint8_t)(buses >> 8);
        uint8_t subordinate = (uint8_t)(buses >> 16);
        info.secondary_bus = secondary;
        info.subordinate_bus = subordinate;
        /* A bridge with no valid downstream bus is still observable, but
         * must not lead to speculative config reads or a traversal loop. */
        if (primary != bus || !secondary || secondary == bus ||
            subordinate < secondary || seen[secondary]) {
            ++stats->bridges_ignored;
        } else {
            /* Every bus can be queued only once: at most 256 entries. */
            if (*tail >= NAS_PCI_SCAN_MAX_BUSES) return -ENOSPC;
            seen[secondary] = 1U;
            queue[(*tail)++] = secondary;
        }
    }
    ++stats->functions;
    return visit(visitor_context, &info);
}

int nas_pci_scan_root(const struct nas_pci_config_reader *reader,
                      uint8_t root_bus, nas_pci_function_visit visit,
                      void *visitor_context, struct nas_pci_scan_stats *stats) {
    if (!reader || !reader->read32 || !visit || !stats) return -EINVAL;
    struct nas_pci_scan_stats result = {0};
    uint8_t seen[NAS_PCI_SCAN_MAX_BUSES] = {0};
    uint8_t queue[NAS_PCI_SCAN_MAX_BUSES] = {0};
    unsigned head = 0U, tail = 1U;
    queue[0] = root_bus;
    seen[root_bus] = 1U;
    while (head < tail) {
        uint8_t bus = queue[head++];
        ++result.buses;
        for (uint8_t slot = 0; slot < 32U; ++slot) {
            uint32_t first = read32(reader, bus, slot, 0, 0U);
            if ((uint16_t)first == UINT16_MAX) continue;
            uint32_t header = read32(reader, bus, slot, 0, 0x0cU);
            unsigned functions = (header & (UINT32_C(1) << 23)) ? 8U : 1U;
            for (unsigned fun = 0; fun < functions; ++fun) {
                uint32_t identity = fun ? read32(reader, bus, slot,
                                                (uint8_t)fun, 0U) : first;
                if ((uint16_t)identity == UINT16_MAX) continue;
                int rc = scan_function(reader, bus, slot, (uint8_t)fun,
                                       identity, visit, visitor_context,
                                       &result, seen, queue, &tail);
                if (rc) { *stats = result; return rc; }
            }
        }
    }
    *stats = result;
    return 0;
}
