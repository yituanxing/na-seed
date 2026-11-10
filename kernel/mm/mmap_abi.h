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
    /* MAP_DENYWRITE is an obsolete, ignored Linux loader hint (glibc uses
     * it even for the initial PROT_NONE anonymous DSO reservation).
     * MAP_NORESERVE and MAP_STACK likewise do not change ownership. */
    const uint64_t optional=0x800ULL | 0x4000ULL | 0x20000ULL;
    return (flags & 0x22ULL)==0x22ULL &&
           !(flags & ~(0x22ULL|optional)) && offset==0;
}
/* MAP_SHARED|MAP_ANONYMOUS creates shared page identity even across fork.
 * This is not interchangeable with a private COW mapping. */
static inline bool nas_linux_anon_shared_map_supported(uint64_t flags,
                                                       uint64_t offset) {
    const uint64_t optional=0x800ULL | 0x4000ULL | 0x20000ULL;
    return (flags & 0x23ULL)==0x21ULL &&
           !(flags & ~(0x21ULL|optional)) && offset==0;
}
/* A MAP_FIXED anonymous remap is only admitted if VM already owns the
 * whole virtual interval; ordinary base mmap flags remain unchanged. */
static inline bool nas_linux_anon_fixed_supported(uint64_t flags,
                                                   uint64_t offset) {
    const uint64_t optional=0x800ULL | 0x4000ULL | 0x20000ULL;
    return (flags & 0x32ULL)==0x32ULL &&
           !(flags & ~(0x32ULL|optional)) && offset==0;
}
#endif
