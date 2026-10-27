#ifndef NASEED_VFS_H
#define NASEED_VFS_H
#include <stdint.h>
#include <stdbool.h>
/* Registered VFS providers; kernel-owned buffers only, never Linux user ABI.
 * nas_fs_node is a borrowed inode snapshot, not an owning open file. */
struct nas_fs_node;
struct nas_vfs_mount;
struct nas_vfs_type {
    const char *name;
    /* Human-readable source label for stable exec acceptance diagnostics;
     * does not participate in dispatch or inode ownership. */
    const char *exec_source_label;
    int (*lookup)(const struct nas_vfs_mount *mount, const char *absolute,
                  struct nas_fs_node *);
    int (*readlink)(const struct nas_fs_node *,const char *absolute,
                    char target[256]);
    int64_t (*read_at)(const struct nas_fs_node *, uint64_t offset,
                       uint8_t *kernel_dst, uint64_t length);
    int (*chmod)(const struct nas_fs_node *, uint64_t mode);
    int (*utimens)(const struct nas_fs_node *, uint64_t atime,
                   uint64_t mtime, bool set_atime, bool set_mtime);
    bool read_only;
};
struct nas_vfs_mount {
    const char *mountpoint; /* Static, stable absolute path */
    const struct nas_vfs_type *type;
    /* Legacy /tmp overlay can still expose CPIO descendants; real disk
     * mounts must never use this compatibility fallback. */
    bool fallback_on_miss;
    bool permanent; /* bootstrap filesystem mounts cannot be detached */
    bool read_only;  /* per-mount restriction */
    void *private_data; /* owning superblock/device adapter */
};
/* A shared OPEN File pins its mount exactly once, not once per FD alias. */
int nas_vfs_unregister_mount(const struct nas_vfs_mount *mount);
int nas_vfs_node_pin(const struct nas_fs_node *node);
int nas_vfs_node_unpin(const struct nas_fs_node *node);
int nas_vfs_register_mount(const struct nas_vfs_mount *mount);
int nas_vfs_lookup(const char *absolute, struct nas_fs_node *node);
int nas_vfs_readlink(const struct nas_fs_node *node,const char *absolute,
                     char target[256]);
int64_t nas_vfs_read_at(const struct nas_fs_node *node, uint64_t offset,
                         uint8_t *dst, uint64_t count);
int nas_vfs_chmod(const struct nas_fs_node *node, uint64_t mode);
int nas_vfs_utimens(const struct nas_fs_node *node, uint64_t atime,
                    uint64_t mtime, bool set_atime, bool set_mtime);
bool nas_vfs_can_write(const struct nas_fs_node *node);
#endif
