#ifndef NASEED_PLATFORM_FIRMWARE_FDT_H
#define NASEED_PLATFORM_FIRMWARE_FDT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Supported native hardware resources. Values are decoded from the boot
 * device tree once, before PMM/IRQ setup; no firmware pointers escape. */
struct nas_fdt_hardware {
    uint64_t timebase_hz;
    uint64_t plic, plic_enable, plic_context;
    uint64_t gicd, gicc;
    unsigned timer_irq;
    uint64_t virtio_base;
    unsigned virtio_stride, virtio_count, virtio_irq_first;
    uint32_t virtio_edge_mask;
};
extern struct nas_fdt_hardware nas_fdt_hw;
bool nas_fdt_discover(const void *blob, size_t extent, bool riscv, uint64_t hart);
#endif
