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
/* Linux include/uapi/linux/stat.h: statx has the SAME 256-byte layout for
 * x86-64, AArch64 and RV64. Never use the host C library's statx struct. */
struct nas_linux_statx_timestamp {
    int64_t sec;
    uint32_t nsec;
    int32_t reserved;
};
struct nas_linux_statx {
    uint32_t mask, blksize;
    uint64_t attributes;
    uint32_t nlink, uid, gid;
    uint16_t mode, spare0;
    uint64_t ino, size, blocks, attributes_mask;
    struct nas_linux_statx_timestamp atime, btime, ctime, mtime;
    uint32_t rdev_major, rdev_minor, dev_major, dev_minor;
    uint64_t mnt_id;
    uint32_t dio_mem_align, dio_offset_align;
    uint64_t spare3[12];
};
_Static_assert(sizeof(struct nas_linux_statx_timestamp)==16,"statx timestamp ABI");
_Static_assert(sizeof(struct nas_linux_statx)==256,"Linux statx ABI size");
_Static_assert(offsetof(struct nas_linux_statx, ino)==32,"Linux statx ino");
_Static_assert(offsetof(struct nas_linux_statx, atime)==64,"Linux statx atime");
_Static_assert(offsetof(struct nas_linux_statx, dev_major)==136,"Linux statx device");
_Static_assert(offsetof(struct nas_linux_statx, mnt_id)==144,"Linux statx mount ID");
/* Linux LP64 struct statfs: f_fsid is two 32-bit words, followed by
 * 64-bit name length, fragment size, mount flags and four spare words.
 * Both x86-64 and asm-generic LP64 use this same 120-byte wire format. */
struct nas_linux_statfs {
    int64_t type, bsize;
    uint64_t blocks,bfree,bavail,files,ffree;
    int32_t fsid[2];
    int64_t namelen,frsize,flags;
    int64_t spare[4];
};
_Static_assert(sizeof(struct nas_linux_statfs)==120,"LP64 statfs size");
_Static_assert(offsetof(struct nas_linux_statfs,fsid)==56,"LP64 statfs fsid");
_Static_assert(offsetof(struct nas_linux_statfs,namelen)==64,"LP64 statfs name max");
#endif
