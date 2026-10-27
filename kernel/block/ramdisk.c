#include "ramdisk.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
#include <stdint.h>

/* RamDisk exercises the exact block ops that VirtIO-Blk/NVMe will supply.
 * Never let a filesystem reach the buffer by testing the device kind. */
static int ram_read(struct nas_block_device *dev, uint64_t sector,
                    uint32_t count, void *dst) {
    struct nas_ramdisk *rd = (struct nas_ramdisk *)dev->context;
    memcpy(dst, rd->storage + sector * dev->sector_bytes,
           (size_t)count * dev->sector_bytes);
    return 0;
}
static int ram_write(struct nas_block_device *dev, uint64_t sector,
                     uint32_t count, const void *src) {
    struct nas_ramdisk *rd = (struct nas_ramdisk *)dev->context;
    memcpy(rd->storage + sector * dev->sector_bytes, src,
           (size_t)count * dev->sector_bytes);
    return 0;
}
static int ram_flush(struct nas_block_device *dev) {
    (void)dev;
    return 0;
}
static const struct nas_block_ops ram_ops = {
    .read=ram_read, .write=ram_write, .flush=ram_flush
};
int nas_ramdisk_attach(struct nas_ramdisk *rd, const char *name,
                       void *storage, uint64_t bytes,
                       uint32_t sector_bytes, bool writable) {
    if (!rd || !name || !storage || !sector_bytes || !bytes ||
        bytes % sector_bytes || (rd->block.registered || rd->block.refs))
        return -EINVAL;
    rd->storage = (uint8_t *)storage;
    rd->storage_bytes = bytes;
    rd->block = (struct nas_block_device){
        .name = name, .sectors = bytes / sector_bytes,
        .sector_bytes = sector_bytes, .max_sectors_per_io = 128,
        .read_only = !writable, .ops = &ram_ops, .context = rd
    };
    int ret = nas_block_register(&rd->block);
    if (ret) {
        rd->block = (struct nas_block_device){0};
        rd->storage = NULL;
        rd->storage_bytes = 0;
    }
    return ret;
}
int nas_ramdisk_detach(struct nas_ramdisk *rd) {
    if (!rd) return -EINVAL;
    int err = nas_block_unregister(&rd->block);
    if (err) return err;
    rd->block = (struct nas_block_device){0};
    rd->storage = NULL;
    rd->storage_bytes = 0;
    return 0;
}
