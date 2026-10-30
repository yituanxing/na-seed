#include <assert.h>
#include "../kernel/fs/mode.h"
#include "../kernel/arch/linux_fcntl_abi.h"
int main(void) {
    assert(NAS_UMASK_DEFAULT==0022U);
    /* The raw int ABI accepts zero- or sign-extended AT_FDCWD.
     * Real Debian BusyBox tar -C ... src previously returned EBADF. */
    assert(nas_linux_signed_dirfd(0x00000000ffffff9cULL)==-100);
    assert(nas_linux_signed_dirfd(0xffffffffffffff9cULL)==-100);
    assert(nas_linux_signed_dirfd(0x0000000000000007ULL)==7);
    assert(nas_linux_signed_dirfd(0x00000000ffffff9dULL)==-99);
    assert(nas_linux_signed_dirfd(0x100000007ULL)==7);
    assert(nas_fs_umask(0027)==0027);
    assert(nas_fs_umask(~0ULL)==0777);
    assert(nas_fs_create_mode(0666,0022)==0644);
    assert(nas_fs_create_mode(0777,0027)==0750);
    assert(nas_fs_create_mode(0600,0777)==0);
    assert(nas_fs_create_mode(07777,0)==07777);
    assert(nas_fs_create_mode(0100777,0022)==0755);
    return 0;
}
