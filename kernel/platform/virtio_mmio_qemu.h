#ifndef NASEED_PLATFORM_VIRTIO_MMIO_QEMU_H
#define NASEED_PLATFORM_VIRTIO_MMIO_QEMU_H
#include <stdint.h>
#include <stdbool.h>
/* Addresses are machine-specific, not part of block/VFS contract. */
int nas_qemu_virtio_mmio_probe(uint64_t physical, uint64_t virtual_base,
                              bool (*map_page)(uint64_t,uint64_t));
int nas_platform_virtio_blk_probe(void);
#endif
