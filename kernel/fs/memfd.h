#ifndef NASEED_FS_MEMFD_H
#define NASEED_FS_MEMFD_H
#include <stdint.h>

/* Linux memfd_create(2) ABI: user_name is a user-space NUL-terminated name,
 * flags are Linux MFD_* bits. Returns a process FD or negative Linux errno
 * encoded in uint64_t, as for the other NaSeed syscall adapters.
 *
 * Only ordinary unsealable tmpfs-backed memfds are supported initially.
 * Unsupported sealing/hugepage options fail rather than feign success. */
uint64_t nas_fs_memfd_create(uint64_t user_name,uint64_t flags);

#endif
