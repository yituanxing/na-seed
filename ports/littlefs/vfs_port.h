#ifndef NAS_LITTLEFS_VFS_H
#define NAS_LITTLEFS_VFS_H
#include "block_adapter.h"
#include "../../kernel/fs/vfs.h"
#define NAS_LITTLEFS_IDENTITIES 128U
struct nas_littlefs_identity {char path[256];uint64_t ino;};
struct nas_littlefs_vfs {
    struct nas_littlefs_block block;
    lfs_t fs;
    struct nas_vfs_mount mount;
    char mount_point[256];
    uint8_t read_cache[256],write_cache[256],lookahead[16];
    struct nas_littlefs_identity ids[NAS_LITTLEFS_IDENTITIES];
    unsigned count;
    bool mounted;
};
int nas_littlefs_vfs_mount(struct nas_littlefs_vfs *,
                           const char *device,const char *mount_point);
int nas_littlefs_vfs_unmount(struct nas_littlefs_vfs *);
#endif
