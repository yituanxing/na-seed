/* Bounded memfd_create adapter regression. Compile/run directly on host:
 * cc -std=gnu11 -Wall -Wextra -Werror scripts/test-memfd.c -o /tmp/test-memfd
 * /tmp/test-memfd
 * The VFS's real tmpfs sparse/mmap backing is exercised by the existing
 * VFS/file-mmap tests; these mocks test admission and FD/inode publication. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fs/lookup.h"
#include "../kernel/fs/memfd.c"

static unsigned reads,opens,lookups,chmods,unlinks,closes;
static int open_error,lookup_error,chmod_error,unlink_error;
static unsigned collisions;
static uint64_t last_flags,last_mode;
static char first_path[80],last_path[80];
static bool named,open_live;

int nas_user_read_cstr(uint64_t address,char *dst,uint64_t capacity) {
    reads++;
    if(!address || address==1)return -EFAULT;
    const char *name=(const char *)(uintptr_t)address;
    for(uint64_t i=0;i<capacity;i++) {
        dst[i]=name[i];
        if(!name[i])return 0;
    }
    return -ENAMETOOLONG;
}
uint64_t nas_fs_open_path(const char *path,uint64_t flags,uint64_t mode) {
    opens++;
    assert(strncmp(path,"/tmp/.naseed-memfd-",19)==0);
    assert(strlen(path)==35);
    if(!first_path[0])strcpy(first_path,path);
    strcpy(last_path,path);
    last_flags=flags;last_mode=mode;
    if(collisions) {collisions--;return (uint64_t)-EEXIST;}
    if(open_error)return (uint64_t)(int64_t)open_error;
    named=true;open_live=true;
    return 7;
}
int nas_vfs_lookup(const char *path,struct nas_fs_node *out) {
    lookups++;
    assert(!strcmp(path,last_path) && named);
    memset(out,0,sizeof(*out));
    return lookup_error;
}
int nas_vfs_chmod(const struct nas_fs_node *inode,uint64_t mode) {
    (void)inode;
    chmods++;
    assert(mode==0777U);
    return chmod_error;
}
int nas_vfs_unlink(const char *path,uint64_t flags) {
    unlinks++;
    assert(!strcmp(path,last_path) && flags==0 && named);
    if(unlink_error)return unlink_error;
    named=false;
    return 0;
}
void nas_fs_close_fd(uint64_t fd) {
    closes++;
    assert(fd==7 && open_live);
    open_live=false;
}
static void clear_events(void) {
    reads=opens=lookups=chmods=unlinks=closes=0;
    open_error=lookup_error=chmod_error=unlink_error=0;
    collisions=0;last_flags=last_mode=0;
    first_path[0]=last_path[0]=0;
    named=open_live=false;
}
int main(void) {
    clear_events();
    const char *name="xfs_repair: AG 0 / private";
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x9U)==
           (uint64_t)-EINVAL);
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x2U)==
           (uint64_t)-EOPNOTSUPP);
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x4U)==
           (uint64_t)-EOPNOTSUPP);
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0x10U)==
           (uint64_t)-EINVAL);
    assert(reads==0 && opens==0);

    assert(nas_fs_memfd_create(0,1U)==(uint64_t)-EFAULT);
    assert(reads==1 && opens==0);
    assert(nas_fs_memfd_create(1,0)==(uint64_t)-EFAULT);
    char huge[251];memset(huge,'z',sizeof(huge));huge[250]=0;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)huge,1U)==
           (uint64_t)-ENAMETOOLONG);
    assert(opens==0);

    clear_events();
    uint64_t fd=nas_fs_memfd_create((uint64_t)(uintptr_t)name,1U);
    assert(fd==7 && reads==1 && opens==1);
    assert(lookups==1 && chmods==1 && unlinks==1 && closes==0);
    assert(last_flags==(0x2ULL|0x40ULL|0x80ULL|0x80000ULL));
    assert(last_mode==0777U);
    assert(!named && open_live); /* unlinked inode survives on its FD */

    clear_events();
    collisions=2;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)"",0)==7);
    assert(opens==3 && lookups==1 && chmods==1 && unlinks==1);
    assert(last_flags==(0x2ULL|0x40ULL|0x80ULL));
    assert(strcmp(first_path,last_path)!=0);
    assert(!named && open_live);

    clear_events();
    open_error=-ENOSPC;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,1U)==
           (uint64_t)-ENOSPC);
    assert(opens==1 && lookups==0 && unlinks==0 && closes==0);

    clear_events();
    lookup_error=-EIO;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,1U)==
           (uint64_t)-EIO);
    assert(opens==1 && lookups==1 && chmods==0 && unlinks==1 && closes==1);
    assert(!named && !open_live);

    clear_events();
    chmod_error=-EACCES;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0)==
           (uint64_t)-EACCES);
    assert(opens==1 && lookups==1 && chmods==1 && unlinks==1 && closes==1);
    assert(!named && !open_live);

    clear_events();
    unlink_error=-EBUSY;
    assert(nas_fs_memfd_create((uint64_t)(uintptr_t)name,0)==
           (uint64_t)-EBUSY);
    assert(opens==1 && lookups==1 && chmods==1 && unlinks==1 && closes==1);
    assert(named && !open_live); /* no successful orphan claim on failure */
    puts("memfd syscall ABI / real VFS object lifecycle: PASS");
    return 0;
}
