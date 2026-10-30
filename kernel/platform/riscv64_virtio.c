/* QEMU RISC-V virt modern VirtIO-MMIO, same shared driver as ARM.
 * Device address belongs to the platform; no block protocol duplication. */
#include "virtio_mmio_qemu.h"
#include <stdint.h>
#include <stdbool.h>
extern bool nas_rv_map_io_page(uint64_t,uint64_t);
int nas_platform_virtio_blk_probe(void) {
    return nas_qemu_virtio_mmio_probe(UINT64_C(0x10001000),
                                      UINT64_C(0xffffffffe1000000),
                                      0x1000U,8U,nas_rv_map_io_page);
}
