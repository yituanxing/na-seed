#ifndef NASEED_FS_SYSCALL_H
#define NASEED_FS_SYSCALL_H
#include <stdint.h>
#include <stdbool.h>
struct nas_fs_result {
    bool handled;
    uint64_t value;
};
/* Only dispatch actual FS/FD syscall numbers; do not mask unknown syscalls. */
struct nas_fs_result nas_fs_syscall(const uint64_t *args);
/* Existing path resolution adapter is needed by common execve. */
uint64_t nas_fs_resolve_path(uint64_t dirfd, uint64_t pathname, char out[256]);
#endif
