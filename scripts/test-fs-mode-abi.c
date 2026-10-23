#include <assert.h>
#include "../kernel/fs/mode.h"
int main(void) {
    assert(NAS_UMASK_DEFAULT==0022U);
    assert(nas_fs_umask(0027)==0027);
    assert(nas_fs_umask(~0ULL)==0777);
    assert(nas_fs_create_mode(0666,0022)==0644);
    assert(nas_fs_create_mode(0777,0027)==0750);
    assert(nas_fs_create_mode(0600,0777)==0);
    assert(nas_fs_create_mode(07777,0)==07777);
    assert(nas_fs_create_mode(0100777,0022)==0755);
    return 0;
}
