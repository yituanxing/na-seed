/* Linux memfd syscall ABI and pathless File factory admission. Run on host:
 * cc -std=gnu11 -Wall -Wextra -Werror scripts/test-memfd.c -o /tmp/test-memfd
 * /tmp/test-memfd
 * The real VFS/provider inode pin and orphan reclaim are tested separately
 * by the provider fixture; no fake /tmp O_CREAT/unlink path exists here. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fs/memfd.c"

static unsigned reads,creates;
static uint64_t result_fd=7;
static bool last_cloexec;
static char last_name[250];

int nas_user_read_cstr(uint64_t address,char *dst,uint64_t capacity) {
    ++reads;
    if(!address || address==1)return -EFAULT;
    const char *name=(const char *)(uintptr_t)address;
    for(uint64_t i=0;i<capacity;i++) {
        dst[i]=name[i];
        if(!name[i])return 0;
    }
    return -ENAMETOOLONG;
}
uint64_t nas_fs_open_anon_tmpfs(const char *name,bool cloexec) {
    assert(name && strlen(name)<sizeof(last_name));
    ++creates;
    strcpy(last_name,name);
    last_cloexec=cloexec;
    return result_fd;
}
static void reset(void) {
    reads=creates=0;
    result_fd=7;
    last_cloexec=false;
    last_name[0]=0;
}
int main(void) {
    const char *name="xfs_repair: AG 0 / private";
    reset();
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x9U)==
           (uint64_t)-EINVAL);
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x2U)==
           (uint64_t)-EOPNOTSUPP);
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x4U)==
           (uint64_t)-EOPNOTSUPP);
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x10U)==
           (uint64_t)-EINVAL);
    assert(reads==0 && creates==0);

    assert(nas_fs_memfd_create(0,1U)==(uint64_t)-EFAULT);
    assert(reads==1 && creates==0);
    assert(nas_fs_memfd_create(1,0)==(uint64_t)-EFAULT);
    char too_long[251];memset(too_long,'z',sizeof(too_long));
    too_long[250]=0;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)too_long,1U)==
           (uint64_t)-ENAMETOOLONG);
    assert(creates==0);

    reset();
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,1U)==7);
    assert(reads==1 && creates==1 && last_cloexec);
    assert(!strcmp(last_name,name)); /* slash is descriptive, not path */
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)"",0)==7);
    assert(creates==2 && !last_cloexec && !last_name[0]);

    /* Full Linux name capacity is 249 bytes, without any filesystem
     * filename component limit, EEXIST collision loop or /tmp dependency. */
    char maximum[250];memset(maximum,'m',249);maximum[249]=0;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)maximum,0)==7);
    assert(creates==3 && strlen(last_name)==249);

    result_fd=(uint64_t)-EMFILE;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,1U)==
           (uint64_t)-EMFILE);
    result_fd=(uint64_t)-ENOSPC;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0)==
           (uint64_t)-ENOSPC);
    result_fd=(uint64_t)-ENOMEM;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0)==
           (uint64_t)-ENOMEM);
    puts("memfd syscall ABI / pathless VFS inode request: PASS");
    return 0;
}
