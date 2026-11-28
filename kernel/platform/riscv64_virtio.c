/* Native VirtIO-MMIO resources from the validated device tree. */
#include "virtio_mmio_qemu.h"
#include "firmware_fdt.h"
extern bool nas_rv_map_io_page(uint64_t,uint64_t);
extern void nas_rv_plic_attach(volatile uint32_t *,volatile uint32_t *,
                               volatile uint32_t *);
#define RV_PLIC_VIRT UINT64_C(0xffffffffe1010000)
static bool plic_mapped;
int nas_platform_virtio_blk_probe(void) {
    if (!nas_fdt_hw.virtio_count) return 0;
    if (!plic_mapped) {
        uint64_t enable = nas_fdt_hw.plic_enable;
        if (!nas_rv_map_io_page(nas_fdt_hw.plic,RV_PLIC_VIRT) ||
            !nas_rv_map_io_page(enable & ~UINT64_C(4095),RV_PLIC_VIRT+0x1000U) ||
            !nas_rv_map_io_page(nas_fdt_hw.plic_context,RV_PLIC_VIRT+0x2000U))
            return -1;
        nas_rv_plic_attach((volatile uint32_t *)(uintptr_t)RV_PLIC_VIRT,
            (volatile uint32_t *)(uintptr_t)(RV_PLIC_VIRT+0x1000U+(enable&4095U)),
            (volatile uint32_t *)(uintptr_t)(RV_PLIC_VIRT+0x2000U));
        plic_mapped = true;
    }
    return nas_qemu_virtio_mmio_probe(nas_fdt_hw.virtio_base,
        UINT64_C(0xffffffffe1000000),nas_fdt_hw.virtio_stride,
        nas_fdt_hw.virtio_count,nas_rv_map_io_page,
        NAS_IRQ_DOMAIN_RISCV_PLIC,nas_fdt_hw.virtio_irq_first);
}
