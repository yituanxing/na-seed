#ifndef NASEED_LINUX_STAT_ABI_H
#define NASEED_LINUX_STAT_ABI_H
#include <stdint.h>
#include <stddef.h>
/* Linux native stat wire format; one FS implementation, per-ISA packing.
 * Source: Linux arch/x86/include/uapi/asm/stat.h and
 * include/uapi/asm-generic/stat.h. Never use the host's libc layout. */
struct nas_linux_stat {
    uint64_t dev, ino;
#if defined(__x86_64__)
    uint64_t nlink;
    uint32_t mode, uid, gid, pad0;
    uint64_t rdev;
    int64_t size, blksize, blocks;
#elif defined(__aarch64__) || (defined(__riscv) && __riscv_xlen == 64)
    uint32_t mode, nlink, uid, gid;
    uint64_t rdev, pad1;
    int64_t size;
    int32_t blksize, pad2;
    int64_t blocks;
#else
# error "Linux stat ABI missing for this target"
#endif
    int64_t atime_sec;
    uint64_t atime_nsec;
    int64_t mtime_sec;
    uint64_t mtime_nsec;
    int64_t ctime_sec;
    uint64_t ctime_nsec;
#if defined(__x86_64__)
    int64_t unused[3];
#else
    uint32_t unused4, unused5;
#endif
};
#if defined(__x86_64__)
_Static_assert(sizeof(struct nas_linux_stat) == 144, "x86-64 stat size");
_Static_assert(offsetof(struct nas_linux_stat, nlink) == 16, "x86 nlink");
_Static_assert(offsetof(struct nas_linux_stat, mode) == 24, "x86 mode");
#else
_Static_assert(sizeof(struct nas_linux_stat) == 128, "generic 64 stat size");
_Static_assert(offsetof(struct nas_linux_stat, mode) == 16, "generic mode");
_Static_assert(offsetof(struct nas_linux_stat, nlink) == 20, "generic nlink");
#endif
#if defined(__x86_64__)
_Static_assert(offsetof(struct nas_linux_stat, rdev) == 40, "x86 rdev");
#else
_Static_assert(offsetof(struct nas_linux_stat, rdev) == 32, "generic64 rdev");
#endif
_Static_assert(offsetof(struct nas_linux_stat, size) == 48, "size");
_Static_assert(offsetof(struct nas_linux_stat, blocks) == 64, "blocks");
_Static_assert(offsetof(struct nas_linux_stat, atime_sec) == 72, "timestamps");
#endif
