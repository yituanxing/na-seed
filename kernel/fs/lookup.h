#ifndef NASEED_FS_LOOKUP_H
#define NASEED_FS_LOOKUP_H
#include <stdint.h>
#include <stdbool.h>
#include "vfs.h"
/* Borrowed inode view. No concrete bootfs/tmpfs/procfs variants or pointers
 * leak into common pathwalk, ELF loader or future ext4 integration. */
struct nas_fs_node {
    const struct nas_vfs_type *filesystem;
    const struct nas_vfs_mount *mount; /* origin mount, borrowed until pinned */
    const void *inode;
    /* Filesystem-supplied stable inode/stat identity. VFS never exports
     * a provider pointer as st_ino. Zero means metadata unavailable. */
    uint64_t ino;
    uint64_t nlink;
    uint32_t uid,gid;
    uint64_t atime_sec,mtime_sec,ctime_sec;
    uint32_t atime_nsec,mtime_nsec,ctime_nsec; /* provider-owned precision */
    const uint8_t *data;  /* optional zero-copy immutable data */
    uint64_t size;
    uint32_t mode;
    bool is_dir, is_symlink;
};
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]);
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *result,char resolved[256]);
#endif
