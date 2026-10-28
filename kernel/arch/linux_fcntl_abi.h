#ifndef NASEED_LINUX_FCNTL_ABI_H
#define NASEED_LINUX_FCNTL_ABI_H
#include <stdbool.h>
#include <stdint.h>
#define NAS_LINUX_O_PATH 0x200000ULL
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
#endif
