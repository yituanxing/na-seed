#ifndef NASEED_FS_LOOKUP_H
#define NASEED_FS_LOOKUP_H
#include <stdint.h>
#include <stdbool.h>
/* Kernel-owned, borrowed inode view independent of Linux syscall ABI and
 * ELF segment loader. Backends retain storage/namespace ownership. */
enum nas_fs_backend { NAS_FS_BOOTFS, NAS_FS_TMPFS };
struct nas_fs_node {
    enum nas_fs_backend backend;
    const uint8_t *data;
    uint64_t size;
    uint32_t mode;
    bool is_dir;
};
int nas_fs_lookup_node(const char *path, bool follow_final,
                       struct nas_fs_node *result, char resolved[256]);
#endif
