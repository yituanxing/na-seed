#include <assert.h>
#include <stdint.h>
#include "../kernel/arch/linux_fcntl_abi.h"
/* Executed on host x86, but tests actual numeric ARM64 Linux UAPI flags.
 * No guest or libc dependency; O_DIRECT must never become O_DIRECTORY.
 */
int main(void) {
    uint64_t converted = UINT64_MAX;
#if defined(__aarch64__)
    assert(NAS_LINUX_NATIVE_O_DIRECTORY == 040000ULL);
#else
    assert(NAS_LINUX_NATIVE_O_DIRECTORY == 0x10000ULL);
#endif
    assert(nas_linux_arm64_open_flags(0, &converted) && converted == 0);
    assert(nas_linux_arm64_open_flags(040000ULL, &converted));
    assert(converted == 0x10000ULL); /* O_DIRECTORY */
    assert(nas_linux_arm64_open_flags(0100000ULL, &converted));
    assert(converted == 0x20000ULL); /* O_NOFOLLOW */
    assert(nas_linux_arm64_open_flags(0400000ULL, &converted));
    assert(converted == 0x8000ULL); /* O_LARGEFILE */
    assert(nas_linux_arm64_open_flags(040000ULL | 0100000ULL |
                                     0400000ULL | 02000000ULL |
                                     0x800ULL, &converted));
    assert(converted == (0x10000ULL | 0x20000ULL | 0x8000ULL |
                         0x80000ULL | 0x800ULL)); /* CLOEXEC/NONBLOCK */
    converted = 0xdeadbeefULL;
    assert(!nas_linux_arm64_open_flags(0200000ULL, &converted));
    assert(converted == 0xdeadbeefULL); /* reject, never quietly approximate */
    assert(!nas_linux_arm64_open_flags(0, 0));
    assert(nas_linux_path_only_flags(NAS_LINUX_O_PATH | 0x80000ULL |
       0x10000ULL | 0x40ULL | 0x200ULL | 1ULL)==
       (NAS_LINUX_O_PATH | 0x80000ULL | 0x10000ULL));
    assert(nas_linux_path_only_flags(0x40ULL|0x200ULL)==(0x40ULL|0x200ULL));
    assert(nas_linux_arm64_open_flags(NAS_LINUX_O_PATH|040000ULL,&converted));
    assert(nas_linux_path_only_flags(converted)==(NAS_LINUX_O_PATH|0x10000ULL));
#if defined(__x86_64__)
    assert(nas_linux_open_flags(0x10000ULL | 0x80000ULL, &converted));
    assert(converted == (0x10000ULL | 0x80000ULL));
#elif defined(__aarch64__)
    assert(nas_linux_open_flags(040000ULL, &converted));
    assert(converted == 0x10000ULL);
#endif
    return 0;
}
