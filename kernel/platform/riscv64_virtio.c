/* QEMU RISC-V virt modern VirtIO-MMIO, same shared driver as ARM.
 * Device address belongs to the platform; no block protocol duplication. */
#include "virtio_mmio_qemu.h"
#include "../drivers/core/irq.h"
#include <stdint.h>
#include <stdbool.h>
extern bool nas_rv_map_io_page(uint64_t,uint64_t);
extern void nas_rv_plic_attach(volatile uint32_t *,volatile uint32_t *,
                               volatile uint32_t *);
/* QEMU 'virt': external interrupt controller pages for hart 0 supervisor.
 * Only the priority, enable and claim/complete views are mapped; the PLIC
 * window between these offsets remains unmapped. */
#define RV_PLIC_PHYS UINT64_C(0x0c000000)
#define RV_PLIC_VIRT UINT64_C(0xffffffffe1010000)
static bool plic_mapped;
int nas_platform_virtio_blk_probe(void) {
    if (!plic_mapped) {
        if (!nas_rv_map_io_page(RV_PLIC_PHYS,RV_PLIC_VIRT) ||
            !nas_rv_map_io_page(RV_PLIC_PHYS+UINT64_C(0x2000),
                                RV_PLIC_VIRT+UINT64_C(0x1000)) ||
            !nas_rv_map_io_page(RV_PLIC_PHYS+UINT64_C(0x201000),
                                RV_PLIC_VIRT+UINT64_C(0x2000)))
            return -1;
        nas_rv_plic_attach(
            (volatile uint32_t *)(uintptr_t)RV_PLIC_VIRT,
            (volatile uint32_t *)(uintptr_t)(RV_PLIC_VIRT+UINT64_C(0x1080)),
            (volatile uint32_t *)(uintptr_t)(RV_PLIC_VIRT+UINT64_C(0x2000)));
        plic_mapped=true;
    }
    return nas_qemu_virtio_mmio_probe(UINT64_C(0x10001000),
                                      UINT64_C(0xffffffffe1000000),
                                      0x1000U,8U,nas_rv_map_io_page,
                                      NAS_IRQ_DOMAIN_RISCV_PLIC,1U);
}
