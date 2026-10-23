#ifndef NASEED_FS_MODE_H
#define NASEED_FS_MODE_H
#include <stdint.h>
/* Per-process Linux mask: applies ONLY at inode creation, not chmod. */
#define NAS_UMASK_DEFAULT 0022U
static inline uint32_t nas_fs_umask(uint64_t mask) {
    return (uint32_t)(mask & 0777U);
}
static inline uint32_t nas_fs_create_mode(uint64_t requested,
                                           uint32_t current_mask) {
    return (uint32_t)((requested & 07777U) & ~current_mask);
}
#endif
