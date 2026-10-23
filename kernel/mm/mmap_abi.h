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
    /* MAP_NORESERVE is a reservation hint and MAP_STACK is a no-op on
     * Linux-compatible generic VMAs; neither changes private ownership. */
    const uint64_t optional=0x4000ULL | 0x20000ULL;
    return (flags & 0x22ULL)==0x22ULL &&
           !(flags & ~(0x22ULL|optional)) && offset==0;
}
/* A MAP_FIXED anonymous remap is only admitted if VM already owns the
 * whole virtual interval; ordinary base mmap flags remain unchanged. */
static inline bool nas_linux_anon_fixed_supported(uint64_t flags,
                                                   uint64_t offset) {
    const uint64_t optional=0x4000ULL | 0x20000ULL;
    return (flags & 0x32ULL)==0x32ULL &&
           !(flags & ~(0x32ULL|optional)) && offset==0;
}
#endif
