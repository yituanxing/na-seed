#ifndef NASEED_UPSTREAM_RAMFS_VFS_PORT_H
#define NASEED_UPSTREAM_RAMFS_VFS_PORT_H
#include "../../kernel/fs/vfs.h"
#include "../../third_party/jkent_ramfs/include/ramfs/ramfs.h"
/* Each mount has independent namespace, inode generations and upstream FS.
 * Stable identities are adapter-owned; no Linux FD or user ABI leaks here. */
#define NAS_RAMFS_IDENTITIES 128U
struct nas_ramfs_identity {
    ramfs_entry_t *entry;
    struct nas_ramfs_identity *parent;
    uint64_t ino;
    uint32_t mode;
    char name[256];
    bool active;
};
struct nas_ramfs_vfs {
    struct nas_vfs_mount mount;
    ramfs_fs_t *fs;
    struct nas_ramfs_identity ids[NAS_RAMFS_IDENTITIES];
    uint64_t next_ino;
    bool mounted;
};
int nas_ramfs_vfs_mount(struct nas_ramfs_vfs *ctx,const char *mountpoint);
int nas_ramfs_vfs_unmount(struct nas_ramfs_vfs *ctx);
#endif
