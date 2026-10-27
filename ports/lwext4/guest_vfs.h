#ifndef NASEED_LWEXT4_GUEST_VFS_H
#define NASEED_LWEXT4_GUEST_VFS_H
#include <stdbool.h>
#include <stdint.h>
#include "lwext4_block.h"
#include "../../kernel/block/ramdisk.h"
#include "../../kernel/fs/vfs.h"
struct ext4_mountpoint;
struct ext4_sblock;

/* One independently mountable ext4 filesystem. The caller owns the
 * persistent source image and this context through final unmount.
 * No filesystem type, ISA or direct userpointer dispatch in this port.
 */
struct nas_lwext4_mount {
    struct nas_ramdisk ramdisk;
    struct nas_lwext4_device device;
    struct nas_vfs_mount mount;
    struct ext4_mountpoint *upstream_mount;
    struct ext4_sblock *upstream_super;
    char mount_point[256];
    char upstream_prefix[256];
    char block_name[32];
    char device_name[32];
    bool mounted;
};
/* Initial guest filesystem implementation is READ-ONLY until generic
 * VFS writes, namespace mutations and unlink-open semantics are available.
 * The underlying lwext4 upstream itself is tested read/write separately. */
int nas_lwext4_ramdisk_mount(struct nas_lwext4_mount *mount,
                             const char *block_name,const char *device_name,
                             const char *mount_point,void *image,
                             uint64_t image_bytes);
int nas_lwext4_ramdisk_unmount(struct nas_lwext4_mount *mount);
#endif
