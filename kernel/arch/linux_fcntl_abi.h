#ifndef NASEED_LINUX_FCNTL_ABI_H
#define NASEED_LINUX_FCNTL_ABI_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_LINUX_O_PATH 0x200000ULL
#define NAS_LINUX_O_LARGEFILE_INTERNAL 0x8000ULL
#define NAS_LINUX_O_DIRECTORY_INTERNAL 0x10000ULL
#define NAS_LINUX_O_NOFOLLOW_INTERNAL 0x20000ULL
/* Linux *at syscalls declare dfd as an int (32 bits), even on LP64.
 * A real statically linked libc may zero-extend negative AT_FDCWD
 * (0x00000000ffffff9c) instead of using a 64-bit -100 constant.
 * Decode exactly the low 32 bits ONCE, without leaking architecture
 * register-width details into VFS or filesystem providers. */
static inline int64_t nas_linux_signed_dirfd(uint64_t raw) {
    uint32_t low=(uint32_t)raw;
    return (low&0x80000000U) ? (int64_t)low-0x100000000LL :
                               (int64_t)low;
}
/* This is the NATIVE userspace UAPI bit, not the kernel's common
 * post-translation value. AArch64 uses a different O_DIRECTORY bit.
 * Expose the difference here, next to the audited conversion routine,
 * so one C userspace program works on every native ISA without ad-hoc
 * architecture-specific ifdefs in filesystem or syscall callers. */
#if defined(__aarch64__)
#define NAS_LINUX_NATIVE_O_DIRECTORY 040000ULL
#else
#define NAS_LINUX_NATIVE_O_DIRECTORY 0x10000ULL
#endif
/* Linux O_PATH ignores ordinary I/O flags, retaining metadata options. */
static inline uint64_t nas_linux_path_only_flags(uint64_t flags) {
    return (flags & NAS_LINUX_O_PATH) ?
        flags & (NAS_LINUX_O_PATH|0x80000ULL|0x10000ULL|0x20000ULL)
        : flags;
}


/* Linux user O_* flags are an ABI, not a filesystem-specific policy.
 * The shared FS consumes x86-64 / generic64 bit positions internally.
 * Linux AArch64 overrides four O_* values: translate BEFORE common FS.
 * Keep this helper ISA-independent so a host test can verify ARM values
 * without running a full AArch64 QEMU boot.
 */
static inline bool nas_linux_arm64_open_flags(uint64_t native,
                                              uint64_t *internal) {
    if (!internal) return false;
    const uint64_t arm_directory = 040000ULL;
    const uint64_t arm_nofollow  = 0100000ULL;
    const uint64_t arm_direct    = 0200000ULL;
    const uint64_t arm_largefile = 0400000ULL;
    /* No direct-I/O semantics in our memory-backed FS. Refuse O_DIRECT. */
    if (native & arm_direct) return false;
    uint64_t mapped = native & ~(arm_directory | arm_nofollow |
                                 arm_direct | arm_largefile);
    if (native & arm_directory) mapped |= 0x10000ULL;
    if (native & arm_nofollow) mapped |= 0x20000ULL;
    if (native & arm_largefile) mapped |= 0x8000ULL;
    *internal = mapped;
    return true;
}
static inline bool nas_linux_open_flags(uint64_t native, uint64_t *internal) {
#if defined(__aarch64__)
    return nas_linux_arm64_open_flags(native, internal);
#elif defined(__x86_64__) || (defined(__riscv) && __riscv_xlen == 64)
    if (!internal) return false;
    *internal = native;
    return true;
#else
# error "Linux open flag ABI missing for target"
#endif
}
/* File status flags belong to a shared open-file description; descriptor
 * flags (O_CLOEXEC) and creation-only flags (O_CREAT/O_EXCL/O_TRUNC) do not.
 * Linux LP64 sets O_LARGEFILE on ordinary open descriptions even when the
 * caller did not request it. O_PATH is the exception: only its metadata
 * flags survive and it does not gain O_LARGEFILE. Keep the common File
 * representation in generic64 positions, not target-specific UAPI bits. */
static inline uint64_t nas_linux_open_status_flags(uint64_t internal) {
    /* tmpfs is already in-memory coherent; O_DSYNC is an honest accepted
     * File status hint, observable through F_GETFL, with no disk flush. */
    const uint64_t retained=NAS_LINUX_O_PATH|3ULL|0x400ULL|0x800ULL|0x1000ULL|
        NAS_LINUX_O_LARGEFILE_INTERNAL|NAS_LINUX_O_DIRECTORY_INTERNAL|
        NAS_LINUX_O_NOFOLLOW_INTERNAL;
    uint64_t flags=internal&retained;
    if(!(flags&NAS_LINUX_O_PATH))flags|=NAS_LINUX_O_LARGEFILE_INTERNAL;
    return flags;
}
/* fcntl(F_GETFL) is the inverse ABI edge of open/openat. On AArch64 the
 * native O_DIRECTORY, O_NOFOLLOW and O_LARGEFILE positions differ from the
 * common generic64 layout. F_SETFL only mutates O_APPEND/O_NONBLOCK, whose
 * positions are already identical across our three LP64 Linux targets. */
static inline uint64_t nas_linux_arm64_status_flags(uint64_t internal) {
    const uint64_t translated=NAS_LINUX_O_DIRECTORY_INTERNAL|
        NAS_LINUX_O_NOFOLLOW_INTERNAL|NAS_LINUX_O_LARGEFILE_INTERNAL;
    uint64_t native=internal&~translated;
    if(internal&NAS_LINUX_O_DIRECTORY_INTERNAL)native|=040000ULL;
    if(internal&NAS_LINUX_O_NOFOLLOW_INTERNAL)native|=0100000ULL;
    if(internal&NAS_LINUX_O_LARGEFILE_INTERNAL)native|=0400000ULL;
    return native;
}
static inline uint64_t nas_linux_status_flags(uint64_t internal) {
#if defined(__aarch64__)
    return nas_linux_arm64_status_flags(internal);
#elif defined(__x86_64__) || (defined(__riscv) && __riscv_xlen == 64)
    return internal;
#else
# error "Linux fcntl flag ABI missing for target"
#endif
}
#endif
