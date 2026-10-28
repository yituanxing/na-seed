#ifndef NASEED_UPSTREAM_RAMFS_VFS_PORT_H
#define NASEED_UPSTREAM_RAMFS_VFS_PORT_H
#include "../../kernel/fs/vfs.h"
#include "../../third_party/jkent_ramfs/include/ramfs/ramfs.h"
/* Each mount has independent namespace, inode generations and upstream FS.
 * Stable identities are adapter-owned; no Linux FD or user ABI leaks here. */
struct nas_ramfs_identity {
    /* Stable address for each borrowed VFS inode snapshot. Inactive records
     * may be recycled only with a NEW inode generation; the old snapshot's
     * ino will then fail validate_inode with ESTALE. */
    struct nas_ramfs_identity *next;
    ramfs_entry_t *entry;
    struct nas_ramfs_identity *parent;
    uint64_t ino;
    uint32_t mode;
    char name[256];
    bool active;
    bool orphaned; /* VFS-open inode detached into unreachable private store */
};
struct nas_ramfs_vfs {
    struct nas_vfs_mount mount;
    ramfs_fs_t *fs;
    struct nas_ramfs_identity root_inode;
    struct nas_ramfs_identity *identities; /* allocated on demand, per mount */
    struct nas_ramfs_identity *orphan_dir; /* private, never visible to namei */
    uint64_t next_ino;
    bool mounted;
};
int nas_ramfs_vfs_mount(struct nas_ramfs_vfs *ctx,const char *mountpoint);
int nas_ramfs_vfs_unmount(struct nas_ramfs_vfs *ctx);
#endif
