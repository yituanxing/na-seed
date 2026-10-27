#ifndef NASEED_VIRTIO_MMIO_BLK_H
#define NASEED_VIRTIO_MMIO_BLK_H
#include <stdint.h>
/* Transport-independent virtio-blk request backend; the platform owns
 * physical MMIO mapping and passes an already mapped 512-byte register bank.
 * Returns 1 when a block device was actually registered, 0 for non-blk,
 * negative errno for a broken virtio-blk device. */
int nas_virtio_mmio_blk_attach(volatile uint32_t *registers);
unsigned nas_virtio_mmio_blk_count(void);
#endif
