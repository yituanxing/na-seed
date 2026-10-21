#include <assert.h>
#include "../kernel/arch/linux_syscall_abi.h"
int main(void) {
    assert(nas_linux_decode_syscall(__NR_read) == NAS_SC_read);
    assert(nas_linux_decode_syscall(__NR_write) == NAS_SC_write);
    assert(nas_linux_decode_syscall(__NR_fork) == NAS_SC_fork);
    assert(nas_linux_decode_syscall(__NR_execve) == NAS_SC_execve);
    assert(nas_linux_decode_syscall(__NR_exit) == NAS_SC_exit);
    assert(nas_linux_decode_syscall(__NR_mmap) == NAS_SC_mmap);
    assert(nas_linux_decode_syscall(__NR_newfstatat) == NAS_SC_newfstatat);
    assert(nas_linux_decode_syscall(UINT64_MAX) == NAS_SC_NONE);
    return 0;
}
