/* Exercise the production pathname/dirfd ownership adapter, with a small
 * fake Dentry implementation. This is a HOST unit test, not a guest claim. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/errno.h"
#include "../kernel/task.h"
#include "../kernel/fd.h"
#include "../kernel/arch/linux_fcntl_abi.h"
#include "../kernel/fs/path_request.h"
#include "../kernel/arch/mmu.h"

static int rootless,root_refs,cwd_refs,fd_refs;
static char root_dentry,cwd_dentry,fd_dentry;
static struct nas_task task;
static struct nas_fs_context fs;
static struct nas_fd_table fds;
struct nas_task *nas_current_task=&task;
struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];

static int *ref_counter(const struct nas_vfs_path *path) {
    if(path->dentry==(struct nas_vfs_dentry *)&root_dentry)return &root_refs;
    if(path->dentry==(struct nas_vfs_dentry *)&cwd_dentry)return &cwd_refs;
    if(path->dentry==(struct nas_vfs_dentry *)&fd_dentry)return &fd_refs;
    assert(!"unexpected Dentry identity");
    return NULL;
}
int nas_vfs_path_root(struct nas_vfs_path *out) {
    *out=(struct nas_vfs_path){0};
    if(rootless)return -ENOENT;
    out->dentry=(struct nas_vfs_dentry *)&root_dentry;
    root_refs++;
    return 0;
}
int nas_vfs_path_get(const struct nas_vfs_path *from,
                     struct nas_vfs_path *to) {
    assert(from && from->dentry && to);
    ++*ref_counter(from);
    *to=*from;
    return 0;
}
int nas_vfs_path_put(struct nas_vfs_path *path) {
    assert(path && path->dentry);
    assert(*ref_counter(path)>0);
    --*ref_counter(path);
    *path=(struct nas_vfs_path){0};
    return 0;
}
uint8_t *nas_mmu_user_byte(uint64_t address) {
    if(address>=USER_LIMIT)return NULL;
    return (uint8_t *)(uintptr_t)address;
}
void nas_print(const char *message) { fputs(message,stderr); }
void nas_arch_halt(void) { assert(!"kernel panic in path request host test");__builtin_trap(); }

static void setup(void) {
    memset(&task,0,sizeof(task));
    memset(&fs,0,sizeof(fs));
    memset(&fds,0,sizeof(fds));
    memset(nas_open_files,0,sizeof(nas_open_files));
    rootless=0;root_refs=cwd_refs=fd_refs=0;
    strcpy(fs.cwd,"/");
    task.fs=&fs;task.fds=&fds;
}
static void no_leak(void) {
    assert(!root_refs && !cwd_refs && !fd_refs);
}
static void make_cwd(void) {
    fs.cwd_path.dentry=(struct nas_vfs_dentry *)&cwd_dentry;
    fs.cwd_pinned=true;
    cwd_refs=1; /* FS-context's preexisting owner */
}
static void make_dirfd(void) {
    fds.entries[7].kind=FD_DIR;
    fds.entries[7].slot=3;
    nas_open_files[3].vnode_pinned=true;
    nas_open_files[3].vnode.is_dir=true;
    nas_open_files[3].vpath.dentry=(struct nas_vfs_dentry *)&fd_dentry;
    nas_open_files[3].status_flags=NAS_LINUX_O_PATH;
    fd_refs=1; /* File's preexisting owner */
}

int main(void) {
    struct nas_fs_path_request req;
    setup();
    /* Absolute paths never even inspect invalid dirfd, including -99. */
    assert(!nas_fs_kernel_path_begin((uint64_t)-99,"/tmp/file",&req));
    assert(!req.start.dentry && root_refs==1);
    nas_fs_path_end(&req);
    no_leak();

    setup();make_cwd();
    assert(!nas_fs_kernel_path_begin(0x00000000ffffff9cULL,"child",&req));
    assert(req.start.dentry==fs.cwd_path.dentry && cwd_refs==2 && root_refs==1);
    nas_fs_path_end(&req);
    assert(cwd_refs==1 && !root_refs);
    fs.cwd_path=(struct nas_vfs_path){0};cwd_refs=0;
    no_leak();

    setup();make_dirfd();rootless=1;
    /* A real owned rootless mount directory still resolves relative FD
     * paths even though nas_vfs_path_root() returns -ENOENT. */
    assert(!nas_fs_kernel_path_begin(7,"subdir/file",&req));
    assert(req.start.dentry==nas_open_files[3].vpath.dentry);
    assert(!req.root.dentry && fd_refs==2);
    nas_fs_path_end(&req);
    assert(fd_refs==1);
    fd_refs=0;no_leak();

    setup();make_dirfd();
    assert(!nas_fs_kernel_path_begin(0x100000007ULL,"file",&req));
    assert(req.start.dentry==nas_open_files[3].vpath.dentry);
    nas_fs_path_end(&req);
    assert(fd_refs==1 && !root_refs);
    nas_open_files[3].vnode.is_dir=false;
    assert(nas_fs_kernel_path_begin(7,"file",&req)==-ENOTDIR);
    assert(fd_refs==1 && !root_refs);
    fd_refs=0;no_leak();

    setup();make_cwd();
    fs.cwd_path=(struct nas_vfs_path){0};cwd_refs=0;
    assert(nas_fs_kernel_path_begin((uint64_t)-100,"file",&req)==-ESTALE);
    no_leak();

    setup();
    assert(nas_fs_kernel_path_begin((uint64_t)-99,"file",&req)==-EBADF);
    assert(nas_fs_kernel_path_begin((uint64_t)-100,"",&req)==-ENOENT);
    char at_limit[257];memset(at_limit,'x',256);at_limit[256]=0;
    assert(nas_fs_kernel_path_begin((uint64_t)-100,at_limit,&req)==-ENAMETOOLONG);
    at_limit[255]=0;
    assert(!nas_fs_kernel_path_begin((uint64_t)-100,at_limit,&req));
    nas_fs_path_end(&req);no_leak();

    setup();
    assert(nas_fs_user_path_begin((uint64_t)-100,USER_LIMIT,&req)==-EFAULT);
    assert(nas_fs_user_path_begin((uint64_t)-100,
                   (uint64_t)(uintptr_t)"",&req)==-ENOENT);
    assert(!nas_fs_user_path_begin((uint64_t)-100,
                   (uint64_t)(uintptr_t)"/tmp/guest",&req));
    assert(!strcmp(req.pathname,"/tmp/guest"));
    nas_fs_path_end(&req);no_leak();
    puts("[PATH REQUEST HOST PASS] absolute dirfd ignored; rootless relative File; O_PATH; ownership; invalid paths");
    return 0;
}
