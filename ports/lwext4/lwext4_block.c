/* Generic NaSeed BlockDevice <-> real gkostka/lwext4 block-device iface.
 * This code is independent of RamDisk, VirtIO, ISA and kernel syscalls.
 * Upstream lwext4 returns POSITIVE error numbers, NaSeed negative errno. */
#include "lwext4_block.h"
#include <ext4.h>
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stddef.h>
#include <stdint.h>
static struct nas_lwext4_device *bridge(struct ext4_blockdev *bd) {
    if(!bd || !bd->bdif)return NULL;
    return (struct nas_lwext4_device *)bd->bdif->p_user;
}
static int positive(int err) { return err<0 ? -err : err; }
static int drive_open(struct ext4_blockdev *bd) {
    struct nas_lwext4_device *ctx=bridge(bd);
    return ctx && ctx->registered && ctx->storage ? 0 : ENODEV;
}
static int drive_read(struct ext4_blockdev *bd,void *dst,
                      uint64_t sector,uint32_t count) {
    struct nas_lwext4_device *ctx=bridge(bd);
    if(!ctx || !ctx->storage)return ENODEV;
    return positive(nas_block_read(ctx->storage,sector,count,dst));
}
static int drive_write(struct ext4_blockdev *bd,const void *src,
                       uint64_t sector,uint32_t count) {
    struct nas_lwext4_device *ctx=bridge(bd);
    if(!ctx || !ctx->storage)return ENODEV;
    return positive(nas_block_write(ctx->storage,sector,count,src));
}
static int drive_close(struct ext4_blockdev *bd) {
    struct nas_lwext4_device *ctx=bridge(bd);
    if(!ctx || !ctx->storage)return ENODEV;
    return positive(nas_block_flush(ctx->storage));
}
static int drive_lock(struct ext4_blockdev *bd) {(void)bd;return 0;}
static int drive_unlock(struct ext4_blockdev *bd) {(void)bd;return 0;}
int nas_lwext4_device_attach(struct nas_lwext4_device *dev,
                             const char *block_name,const char *ext_name) {
    if(!dev || !block_name || !ext_name)return -EINVAL;
    if(dev->registered || dev->storage)return -EBUSY;
    struct nas_block_device *block=nas_block_get(block_name);
    if(!block)return -ENODEV;
    if(block->sector_bytes>sizeof(dev->physical_buffer)) {
        nas_block_put(block);return -EINVAL;
    }
    dev->storage=block;
    dev->interface=(struct ext4_blockdev_iface){
        .open=drive_open,.bread=drive_read,.bwrite=drive_write,
        .close=drive_close,.lock=drive_lock,.unlock=drive_unlock,
        .ph_bsize=block->sector_bytes,.ph_bcnt=block->sectors,
        .ph_bbuf=dev->physical_buffer,.p_user=dev
    };
    dev->ext=(struct ext4_blockdev){
        .bdif=&dev->interface,.part_offset=0,
        .part_size=block->sectors * block->sector_bytes
    };
    int rc=ext4_device_register(&dev->ext,ext_name);
    if(rc) {
        nas_block_put(block);
        dev->storage=NULL;dev->interface=(struct ext4_blockdev_iface){0};
        dev->ext=(struct ext4_blockdev){0};
        return -rc;
    }
    dev->registered=true;
    return 0;
}
int nas_lwext4_device_detach(struct nas_lwext4_device *dev,
                             const char *ext_name) {
    if(!dev || !ext_name || !dev->registered || !dev->storage)return -EINVAL;
    /* Do not release a device with a still-active upstream mount. */
    if(dev->interface.ph_refctr)return -EBUSY;
    int rc=ext4_device_unregister(ext_name);
    if(rc)return -rc;
    nas_block_put(dev->storage);
    dev->storage=NULL;dev->registered=false;
    dev->ext=(struct ext4_blockdev){0};
    dev->interface=(struct ext4_blockdev_iface){0};
    return 0;
}
