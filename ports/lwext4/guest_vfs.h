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
#define NAS_LWEXT4_ORPHAN_LIMIT 256U
/* A non-owning byte-offset hint for the previous getdents batch. The VFS
 * keeps provider-independent logical cursors; every cache miss rescans. */
struct nas_lwext4_dir_hint {
    uint64_t epoch;
    uint64_t cursor,next_off;
    uint32_t ino;
};
/* A staged orphan may be an ordinary file or an empty directory.
 * The VFS File owns its backend inode until the final unpin; a stable
 * typed private link defers physical reclaim and survives reboot. */
struct nas_lwext4_orphan {
    uint32_t ino,serial;
    bool used,is_dir;
};
struct nas_lwext4_mount {
    struct nas_ramdisk ramdisk;
    struct nas_lwext4_device device;
    struct nas_vfs_mount mount;
    struct ext4_mountpoint *upstream_mount;
    struct ext4_sblock *upstream_super;
    char mount_point[256];
    char upstream_prefix[256];
    char orphan_dir[256];
    uint32_t root_ino,orphan_serial;
    struct nas_lwext4_orphan orphans[NAS_LWEXT4_ORPHAN_LIMIT];
    /* The VFS is currently single-CPU. Any namespace mutation advances the
     * epoch before side effects, invalidating the previous read cursor. */
    uint64_t namespace_epoch;
    struct nas_lwext4_dir_hint directory_hint;
    char block_name[32];
    char device_name[32];
    bool mounted;
    bool owns_ramdisk; /* Only the optional Limine/host RAM image adapter. */
};
/* The generic mount consumes ANY existing BlockDevice: driver ownership,
 * DMA, discovery and interrupt completion stay below BlockDevice.
 * Default guest mount stays RO; writable mounts are explicit. */
int nas_lwext4_device_mount(struct nas_lwext4_mount *mount,
                            const char *block_name,const char *device_name,
                            const char *mount_point,bool writable);
int nas_lwext4_unmount(struct nas_lwext4_mount *mount);
/* Convenience image adapter, using exactly the same generic mount logic.
 * Guest unlink-open and physical durability still require later contracts. */
int nas_lwext4_ramdisk_mount_mode(struct nas_lwext4_mount *mount,
                             const char *block_name,const char *device_name,
                             const char *mount_point,void *image,
                             uint64_t image_bytes,bool writable);
int nas_lwext4_ramdisk_mount(struct nas_lwext4_mount *mount,
                             const char *block_name,const char *device_name,
                             const char *mount_point,void *image,
                             uint64_t image_bytes);
int nas_lwext4_ramdisk_unmount(struct nas_lwext4_mount *mount);
#endif
