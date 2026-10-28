#ifndef NAS_LITTLEFS_BLOCK_ADAPTER_H
#define NAS_LITTLEFS_BLOCK_ADAPTER_H
/* Independently maintained littlefs filesystem via the exact existing
 * kernel-only BlockDevice ABI; deliberately no ISA or VFS syscall policy.
 * Mounting through NaSeed VFS is NOT implemented in this adapter. */
#include "../../third_party/littlefs/lfs.h"
#include "../../kernel/block/device.h"
struct nas_littlefs_block {
    struct nas_block_device *device; /* one owned BlockDevice ref */
    struct lfs_config config;
};
int nas_littlefs_block_attach(struct nas_littlefs_block *adapter,
                               const char *block_name);
void nas_littlefs_block_detach(struct nas_littlefs_block *adapter);
#endif
