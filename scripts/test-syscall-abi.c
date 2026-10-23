#include <assert.h>
#include "../kernel/arch/linux_syscall_abi.h"
int main(void) {
    assert(nas_linux_decode_syscall(__NR_access) == NAS_SC_access);
    assert(nas_linux_decode_syscall(__NR_faccessat) == NAS_SC_faccessat);
    assert(nas_linux_decode_syscall(__NR_faccessat2) == NAS_SC_faccessat2);
    assert(nas_linux_decode_syscall(__NR_read) == NAS_SC_read);
    assert(nas_linux_decode_syscall(__NR_pread64) == NAS_SC_pread64);
    assert(nas_linux_decode_syscall(__NR_pwrite64) == NAS_SC_pwrite64);
    assert(nas_linux_decode_syscall(__NR_write) == NAS_SC_write);
    assert(nas_linux_decode_syscall(__NR_fork) == NAS_SC_fork);
    assert(nas_linux_decode_syscall(__NR_execve) == NAS_SC_execve);
    assert(nas_linux_decode_syscall(__NR_exit) == NAS_SC_exit);
    assert(nas_linux_decode_syscall(__NR_umask) == NAS_SC_umask);
    assert(nas_linux_decode_syscall(__NR_fsync) == NAS_SC_fsync);
    assert(nas_linux_decode_syscall(__NR_fdatasync) == NAS_SC_fdatasync);
    assert(nas_linux_decode_syscall(__NR_ftruncate) == NAS_SC_ftruncate);
    assert(nas_linux_decode_syscall(__NR_truncate) == NAS_SC_truncate);
    assert(nas_linux_decode_syscall(__NR_mmap) == NAS_SC_mmap);
    assert(nas_linux_decode_syscall(__NR_newfstatat) == NAS_SC_newfstatat);
    assert(nas_linux_decode_syscall(UINT64_MAX) == NAS_SC_NONE);
    uint64_t clone_args[7] = {__NR_clone, 17, 0, 0, 0, 0, 0};
    assert(nas_linux_decode_call(clone_args) == NAS_SC_fork);
    clone_args[1] = 17U | 0x100U; /* CLONE_VM: not fork */
    assert(nas_linux_decode_call(clone_args) == NAS_SC_NONE);
    clone_args[1] = 17;
    clone_args[2] = 0x70000000; /* child stack: thread-style clone */
    assert(nas_linux_decode_call(clone_args) == NAS_SC_NONE);
    clone_args[2] = 0;
    clone_args[3] = UINT64_MAX; /* ignored unless CLONE_PARENT_SETTID */
    assert(nas_linux_decode_call(clone_args) == NAS_SC_fork);
    /* Native glibc x86 clone child_tid is ARG4 (r10), not ARG5 (r8 TLS). */
    clone_args[0] = __NR_clone;
    clone_args[1] = 0x01200011U;
    clone_args[2] = 0;
    clone_args[4] = 0x70010000;
    clone_args[5] = UINT64_MAX; /* ignored x86 TLS without CLONE_SETTLS */
    assert(nas_linux_clone_child_tid(clone_args) == 0x70010000);
    assert(nas_linux_decode_call(clone_args) == NAS_SC_fork_child_tid);
    clone_args[4] = 0;
    assert(nas_linux_decode_call(clone_args) == NAS_SC_NONE);
    clone_args[4] = 0x70010000;
    clone_args[1] |= 0x100U; /* CLONE_VM is NOT fork */
    assert(nas_linux_decode_call(clone_args) == NAS_SC_NONE);
    clone_args[1] = 0x01200011U;
    clone_args[0] = __NR_fork;
    assert(nas_linux_decode_call(clone_args) == NAS_SC_fork);
    return 0;
}
