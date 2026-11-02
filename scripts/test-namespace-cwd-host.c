/* Linux namespace syscall adapter over the genuine provider/namei fixture.
 * No QEMU, user VM, or synthetic Dentry constructor: chdir/fchdir/getcwd
 * consume the same VFS Path transactions as the kernel. */
#include <stdlib.h>
#define NAS_PATH_CONTRACT_MAIN_NAME nas_path_contract_fixture_main
#include "test-path-contract-host.c"
#include "../kernel/fs/namespace_syscall.c"

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static struct nas_task cwd_task;
static struct nas_fs_context cwd_fs;
static struct nas_files cwd_files;
struct nas_task *nas_current_task=&cwd_task;

int nas_user_read_cstr(uint64_t address,char *dst,uint64_t capacity) {
    if(!address || !dst || !capacity)return -EFAULT;
    const char *src=(const char *)(uintptr_t)address;
    size_t len=strnlen(src,(size_t)capacity);
    if(len==capacity)return -ENAMETOOLONG;
    memcpy(dst,src,len+1);
    return 0;
}
bool nas_user_write_bytes(uint64_t address,const uint8_t *src,uint64_t len) {
    if(!address || !src)return false;
    memcpy((void *)(uintptr_t)address,src,(size_t)len);
    return true;
}
void nas_print(const char *message) { fputs(message,stderr); }
void nas_arch_halt(void) { abort(); }

int main(void) {
    assert(nas_path_contract_fixture_main()==0);
    cwd_task.fs=&cwd_fs;
    cwd_task.fds=&cwd_files.table;
    strcpy(cwd_fs.cwd,"/");
    deep_many_components=false;
    assert(nas_vfs_register_mount(&deep_mount)==0);

    char pathname[NAS_VFS_WALK_PATH_LIMIT+1];
    size_t length=strlen(deep_mount.mountpoint);
    memcpy(pathname,deep_mount.mountpoint,length);
    for(unsigned depth=0;depth<21;depth++) {
        unsigned component=depth<20?200U:60U;
        pathname[length++]='/';
        memset(pathname+length,depth<20?'x':'y',component);
        length+=component;
    }
    pathname[length]=0;
    assert(length==4095);

    /* chdir must accept a legitimate long owned Path despite the bounded
     * 256-byte cwd display cache. getcwd is then exact, including NUL. */
    assert(sys_chdir((uint64_t)(uintptr_t)pathname)==0);
    assert(cwd_fs.cwd_pinned && cwd_fs.cwd_path.dentry);
    assert(!cwd_fs.cwd[0]);
    char result[NAS_VFS_WALK_PATH_LIMIT+1];
    memset(result,0x5a,sizeof(result));
    assert((int64_t)sys_getcwd_root((uint64_t)(uintptr_t)result,length)
           ==-ERANGE);
    assert(result[0]=='Z' && result[length]=='Z');
    assert(sys_getcwd_root((uint64_t)(uintptr_t)result,length+1)==length+1);
    assert(!strcmp(result,pathname));
    assert(result[length+1]=='Z');

    /* fchdir uses the File's already pinned Dentry; it must neither
     * re-open an origin_path nor fail on its short diagnostic buffer. */
    struct nas_vfs_path path={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,pathname,true,&path)==0);
    struct nas_fs_node inode={0};
    assert(nas_vfs_path_node(&path,&inode)==0);
    assert(nas_vfs_node_pin(&inode)==0);
    struct nas_open_file *opened=&nas_open_files[7];
    opened->vnode=inode;
    opened->vnode_pinned=true;
    assert(nas_vfs_path_get(&path,&opened->vpath)==0);
    cwd_task.fds->entries[3]=(struct nas_fd_entry){FD_DIR,7,0};
    assert(sys_fchdir(3)==0);
    assert(sys_getcwd_root((uint64_t)(uintptr_t)result,length+1)==length+1);
    assert(!strcmp(result,pathname));

    assert(nas_vfs_path_put(&path)==0);
    assert(nas_vfs_path_put(&opened->vpath)==0);
    assert(nas_vfs_node_unpin(&opened->vnode)==0);
    assert(nas_vfs_path_put(&cwd_fs.cwd_path)==0);
    assert(nas_vfs_node_unpin(&cwd_fs.cwd_node)==0);
    assert(nas_vfs_unregister_mount(&deep_mount)==0);
    puts("[NAMESPACE CWD PASS] 4095-byte chdir/fchdir/getcwd, ERANGE and FD Path identity");
    return 0;
}
