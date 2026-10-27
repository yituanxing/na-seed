#ifndef NASEED_FS_LOOKUP_H
#define NASEED_FS_LOOKUP_H
#include <stdint.h>
#include <stdbool.h>
#include "vfs.h"
/* Borrowed inode view. No concrete bootfs/tmpfs/procfs variants or pointers
 * leak into common pathwalk, ELF loader or future ext4 integration. */
struct nas_fs_node {
    const struct nas_vfs_type *filesystem;
    const void *inode;
    const uint8_t *data;  /* optional zero-copy immutable data */
    uint64_t size;
    uint32_t mode;
    bool is_dir, is_symlink;
};
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]);
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *result,char resolved[256]);
#endif
