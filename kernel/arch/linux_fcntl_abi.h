#ifndef NASEED_LINUX_FCNTL_ABI_H
#define NASEED_LINUX_FCNTL_ABI_H
#include <stdbool.h>
#include <stdint.h>

/* Translate Linux userspace O_* bits once at the architecture/FS boundary.
 * Existing FS code uses the x86-64/generic64 layout as its INTERNAL format.
 * AArch64 uniquely overrides several asm-generic O_* values in
 * arch/arm64/include/uapi/asm/fcntl.h.  Raw ARM O_DIRECTORY=040000
 * otherwise looks like an unknown flag and makes BusyBox ls return EINVAL.
 * Do not change the FS backend or accept a flag with different semantics.
 */
static inline bool nas_linux_open_flags(uint64_t native, uint64_t *internal) {
    if (!internal) return false;
#if defined(__aarch64__)
    const uint64_t arm_directory = 040000ULL;
    const uint64_t arm_nofollow = 0100000ULL;
    const uint64_t arm_direct = 0200000ULL;
    const uint64_t arm_largefile = 0400000ULL;
    /* Our memory-backed FS does not implement O_DIRECT alignment/semantics. */
    if (native & arm_direct) return false;
    uint64_t mapped = native & ~(arm_directory | arm_nofollow |
                                 arm_direct | arm_largefile);
    if (native & arm_directory) mapped |= 0x10000ULL; /* O_DIRECTORY */
    if (native & arm_nofollow) mapped |= 0x20000ULL; /* O_NOFOLLOW */
    if (native & arm_largefile) mapped |= 0x8000ULL; /* O_LARGEFILE */
    *internal = mapped;
#elif defined(__x86_64__) || (defined(__riscv) && __riscv_xlen == 64)
    /* x86-64 and generic64 RV64 agree on the supported O_* bit positions. */
    *internal = native;
#else
# error "Linux open flag ABI missing for target"
#endif
    return true;
}
#endif
