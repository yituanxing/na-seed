#ifndef NASEED_MMAP_ABI_H
#define NASEED_MMAP_ABI_H
#include <stdbool.h>
#include <stdint.h>
/* Linux MAP_PRIVATE|MAP_ANONYMOUS. The Linux kernel ignores fd in anonymous
 * mappings. The x86-64 syscall fifth arg can be the zero-extended 32-bit
 * int -1 (0x00000000ffffffff), not always UINT64_MAX. */
static inline bool nas_linux_anon_map_supported(uint64_t flags,
                                                uint64_t fd,
                                                uint64_t offset) {
    (void)fd;
    return (flags & 0x22ULL)==0x22ULL &&
           !(flags & ~0x22ULL) && offset==0;
}
#endif
