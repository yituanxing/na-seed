/* Native VirtIO-MMIO resources from the validated device tree. */
#include "virtio_mmio_qemu.h"
#include "firmware_fdt.h"
extern bool nas_arm_map_io_page(uint64_t,uint64_t);
int nas_platform_virtio_blk_probe(void) {
    if (!nas_fdt_hw.virtio_count) return 0;
    return nas_qemu_virtio_mmio_probe(nas_fdt_hw.virtio_base,
        UINT64_C(0xffffffffe1000000),nas_fdt_hw.virtio_stride,
        nas_fdt_hw.virtio_count,nas_arm_map_io_page,
        NAS_IRQ_DOMAIN_ARM_GIC,nas_fdt_hw.virtio_irq_first);
}
