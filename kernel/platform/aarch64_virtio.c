/* QEMU ARM virt MMIO BAR. The common scanner owns driver discovery;
 * AArch64 owns only the physical address and EL1 Device mapping. */
#include "virtio_mmio_qemu.h"
#include <stdint.h>
#include <stdbool.h>
extern bool nas_arm_map_io_page(uint64_t,uint64_t);
int nas_platform_virtio_blk_probe(void) {
    return nas_qemu_virtio_mmio_probe(UINT64_C(0x0a000000),
                                      UINT64_C(0xffffffffe1000000),
                                      nas_arm_map_io_page);
}
