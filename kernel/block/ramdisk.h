#ifndef NASEED_BLOCK_RAMDISK_H
#define NASEED_BLOCK_RAMDISK_H
#include "device.h"
#include <stdbool.h>
#include <stdint.h>

/* Borrowed, already-addressable backing bytes (e.g. loaded ext4 image).
 * RamDisk does not own or allocate them. No physical-address or ISA policy.
 * A device is initialized once, then detached only after all mount pins end.
 */
struct nas_ramdisk {
    struct nas_block_device block;
    uint8_t *storage;
    uint64_t storage_bytes;
};
int nas_ramdisk_attach(struct nas_ramdisk *rd, const char *name,
                       void *storage, uint64_t bytes,
                       uint32_t sector_bytes, bool writable);
int nas_ramdisk_detach(struct nas_ramdisk *rd);
#endif
