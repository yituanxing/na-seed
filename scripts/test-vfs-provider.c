Warning: truncated output (original token count: 25527)
Total output lines: 1957

/* Test the ACTUAL mount/type/namei code, using controlled backend fixtures.
 * This is a host unit test, not a claimed native ext4 mount. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/bootfs.h"
#include "../kernel/tmpfs.h"
#include "../kernel/fs/procfs.h"
#include "../kernel/fs/lookup.h"
#include "../kernel/fs/vfs.h"
#include "../kernel/fs/posix_locks.h"
#include "../kernel/errno.h"
/* The real tmpfs statfs callback samples physical backing-frame counters.
 * The host fixture controls only that dependency, not VFS dispatch. */
static uint64_t fixture_free_frames=789;
uint64_t nas_managed_frames(void) { return 2048; }
uint64_t nas_free_frames(void) { return fixture_free_frames; }
static const uint8_t elf_bytes[]={0x7f,'E','L','F','v','f','s'};
static const uint8_t abs_link[]="/bin/demo";
static struct bootfs_entry entries[]={
    {.path="/",.mode=0040755,.ino=1},
    {.path="/bin",.mode=0040755,.ino=2},
    {.path="/bin/demo",.mode=0100755,.ino=3,.data=elf_bytes,.size=sizeof(elf_bytes)},
    {.path="/tmp",.mode=0041777,.ino=4},
    {.path="/proc",.mode=0040555,.ino=5},
    {.path="/mnt",.mode=0040755,.ino=6},
    {.path="/tmp/archive",.mode=0100444,.ino=7,.data=elf_bytes,.size=sizeof(elf_bytes)},
    {.path="/shortcut",.mode=0120777,.ino=8,.data=abs_link,.size=sizeof(abs_link)-1}
};
static struct nas_tmp_file tmp_demo={.used=true,.mode=0100644,.size=4,.ino=101};
/* Host backend fixture checks VFS capability routing; the real inode-backed
 * storage/extent traversal is compiled in the native kernel and QEMU runs. */
static char fixture_xattr_name[256];
static uint8_t fixture_xattr_value[64];
static unsigned fixture_xattr_length;
int tmp_setxattr(struct nas_tmp_file *file,const char *name,
                 const uint8_t *value,uint64_t size,unsigned flags) {
    if(file!=&tmp_demo)return -EOPNOTSUPP;
    if(size>sizeof(fixture_xattr_value))return -E2BIG;
    if((flags&1U) && fixture_xattr_name[0])return -EEXIST;
    if((flags&2U) && !fixture_xattr_name[0])return -ENODATA;
    strcpy(fixture_xattr_name,name);
    fixture_xattr_length=(unsigned)size;
    if(size)memcpy(fixture_xattr_value,value,(size_t)size);
    return 0;
}
int64_t tmp_getxattr(const struct nas_tmp_file *file,const char *name,
                     uint8_t *value,uint64_t capacity) {
    if(file!=&tmp_demo || strcmp(name,fixture_xattr_name))return -ENODATA;
    if(value) {
        if(capacity<fixture_xattr_length)return -ERANGE;
        memcpy(value,fixture_xattr_value,fixture_xattr_length);
    }
    return fixture_xattr_length;
}
int64_t tmp_listxattr(const struct nas_tmp_file *file,
                      uint8_t *names,uint64_t capacity) {
    if(file!=&tmp_demo)return -EOPNOTSUPP;
    size_t length=fixture_xattr_name[0]?strlen(fixture_xattr_name)+1:0;
    if(names) {
        if(capacity<length)return -ERANGE;
        memcpy(names,fixture_xattr_name,length);
    }
    return (int64_t)length;
}
int tmp_removexattr(struct nas_tmp_file *file,const char *name) {
    if(file!=&tmp_demo || strcmp(name,fixture_xattr_name))return -ENODATA;
    fixture_xattr_name[0]=0;
    fixture_xattr_length=0;
    return 0;
}
int tmp_fiemap_next(const struct nas_tmp_file *file,uint64_t start,
                    uint64_t end,uint64_t *logical,uint64_t *length,
                    bool *last) {
    if(file!=&tmp_demo)return -EOPNOTSUPP;
    if(start>=4 || end<=start)return 0;
    *logical=0;*length=4096;*last=true;
    return 1;
}
/* VFS capability fixture: the production tmpfs implementation is exercised
 * separately in M108; here the callback must remain provider-scoped and
 * never be inferred for bootfs/procfs or a writable mount flag alone. */
static _Alignas(4096) uint8_t fixture_shared_frame[4096];
static unsigned shared_touch_calls, shared_resize_invalidations;
static unsigned shared_sync_rearms;
int tmp_acquire_shared_page(struct nas_tmp_file *file,uint64_t offset,
                            uint64_t *pa_owned) {
    if(!pa_owned)return -EINVAL;
    *pa_owned=0;
    if(file!=&tmp_demo)return -ENODEV;
    if(offset>=file->size)return -EFAULT;
    *pa_owned=(uint64_t)(uintptr_t)fixture_shared_frame;
    return 0;
}
int tmp_page_resident(const struct nas_tmp_file *file,uint64_t offset) {
    if(file!=&tmp_demo)return -ENODEV;
    return offset<file->size ? 1 : 0;
}
int tmp_touch_shared(struct nas_tmp_file *file) {
    if(file!=&tmp_demo)return -ENODEV;
    shared_touch_calls++;
    return 0;
}
void nas_vm_invalidate_file(const struct nas_fs_node *node,uint64_t new_size) {
    (void)node;(void)new_size;
    shared_resize_invalidations++;
}
void nas_vm_rearm_file(const struct nas_fs_node *node) {
    assert(node && node->filesystem);
    shared_sync_rearms++;
}
static struct nas_tmp_file fixture_symlink={.used=true,.mode=0120777,.size=9,.ino=102};
static const char *fixture_link_target="/bin/demo";
static struct nas_tmp_file fixture_dir_link={.used=true,.mode=0120777,.size=4,.ino=103};
static struct nas_tmp_file fixture_created={.used=true,.mode=0100640,.ino=104};
/* AF_UNIX mknod publishes one inode per bind generation. Unlinking the
 * visible name does not revoke a Path holding the previous inode. */
static struct nas_tmp_file fixture_sockets[4];
static unsigned socket_generation,socket_visible_index=4;
/* A named directory is renamed while its original inode remains open. */
static struct nas_tmp_file fixture_open_directory={
    .used=true,.is_dir=true,.mode=0040755,.nlink=1,.ino=105
};
static struct nas_tmp_file fixture_open_child={
    .used=true,.mode=0100600,.nlink=1,.ino=106
};
static struct nas_tmp_file fixture_detached_dir={
    .used=true,.is_dir=true,.mode=0040755,.nlink=1,.ino=109
};
static bool fixture_detached_dir_visible=true;
static struct nas_tmp_file fixture_orphan={
    .used=true,.mode=0100600,.nlink=1,.ino=107
};
/* Saturation identities are REAL distinct, reusable tmp inode slots.
 * The old test fabricated inode numbers pointing at one file and was
 * invalid once the VFS gained a mandatory backend incarnation check. */
static struct nas_tmp_file capacity_fixture[512];
static unsigned orphan_releases;
/* The host VFS fixture borrows an archive inode for /tmp just like the
 * actual provider. Its live root timestamps come from the tmpfs owner. */
void tmp_root_times(uint64_t *mtime_ns,uint64_t *ctime_ns) {
    if(mtime_ns)*mtime_ns=1761868800000000000ULL;
    if(ctime_ns)*ctime_ns=1761868800000000000ULL;
}
/* Keep the standalone VFS provider harness linked to the single live
 * namespace-derived directory-size contract introduced by M123. */
uint64_t tmp_directory_stat_size(const struct nas_tmp_file *directory) {
    return directory && directory->is_dir ?
           40ULL + 20ULL * directory->children.count : 0;
}
uint64_t tmp_root_directory_stat_size(void) {
    unsigned children=0;
    while(tmp_child_entry_inode(&entries[3],children))children++;
    return 40ULL + 20ULL * children;
}
void tmp_inode_discard(struct nas_tmp_file *node) {
    /* An actual kernel reclaims only orphaned, unpinned inodes; the
     * host fixture records the final provider callback. */
    if(node==&fixture_orphan && !node->nlink &&
       !nas_vfs_inode_is_pinned(node)) {
        node->used=false;
        orphan_releases++;
    }
    for(unsigned i=0;i<4;i++)
        if(node==&fixture_sockets[i] && !node->nlink &&
           !nas_vfs_inode_is_pinned(node))node->used=false;
}
static char fixture_dir_path[256]="/tmp/opened";
static char fixture_child_path[256]="/tmp/opened/child";
static uint8_t fixture_created_bytes[32];
static unsigned fixture_created_length;
static bool archive_available=true;
/* Count actual provider probes, not synthetic elapsed host time. This
 * isolates repeated namei work while binding one already-resolved Path. */
static unsigned bootfs_find_calls;
bool bootfs_mounted(void){return archive_available;}
const struct bootfs_entry *bootfs_find(const char *path){
    ++bootfs_find_calls;
    if(!archive_available)return NULL;
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);i++)
        if(!strcmp(entries[i].path,path))return &entries[i];
    return NULL;
}
bool bootfs_is_dir(const struct bootfs_entry *e){
    return e && (e->mode&0170000U)==0040000U;
}
bool bootfs_is_link(const struct bootfs_entry *e){
    return e && (e->mode&0170000U)==0120000U;
}
struct nas_tmp_file *tmp_find(const char *path){
    if(!strcmp(path,"/tmp/m107-socket") && socket_visible_index<4)
        return &fixture_sockets[socket_visible_index];
    if(!strcmp(path,"/tmp/demo"))return &tmp_demo;
    /* Two legal hard-link names for the same backend inode. A rename
     * between them must be a no-op, not one of the provider's mutations. */
    if(!strcmp(path,"/tmp/hardlink-a") ||
       !strcmp(path,"/tmp/hardlink-b"))return &tmp_demo;
    if(!strcmp(path,"/tmp/link"))return &fixture_symlink;
    if(!strcmp(path,"/tmp/alias"))return &fixture_dir_link;
    if(!strcmp(path,"/tmp/newfile") && fixture_created_length)return &fixture_created;
    if(!strcmp(path,fixture_dir_path))return &fixture_open_directory;
    if(!strcmp(path,fixture_child_path))return &fixture_open_child;
    if(fixture_detached_dir_visible && !strcmp(path,"/tmp/to-detach"))
        return &fixture_detached_dir;
    if(!strcmp(path,"/tmp/pinned-orphan"))return &fixture_orphan;
    return NULL;
}
const char *tmp_name(const struct nas_tmp_file *f) {
    if(f==&fixture_open_directory)return fixture_dir_path;
    if(f==&fixture_detached_dir)return "/tmp/to-detach";
    return NULL;
}
bool tmp_is_link(const struct nas_tmp_file *f){
    return f && (f->mode&0170000U)==0120000U;
}
int tmp_link_target(const struct nas_tmp_file *f,char target[256]){
    if(f==&fixture_dir_link){memcpy(target,"/tmp",5);return 0;}
    if(f!=&fixture_symlink)return -EINVAL;
    memcpy(target,fixture_link_target,strlen(fixture_link_target)+1);return 0;
}
int64_t tmp_read_at(const struct nas_tmp_file *file,uint64_t off,uint8_t *buf,uint64_t len){
    static const char value[]="DATA";
    if(file==&fixture_created) {
        if(off>=fixture_created_length)return 0;
        if(len>fixture_created_length-off)len=fixture_created_length-off;
        memcpy(buf,fixture_created_bytes+off,(size_t)len);return (int64_t)len;
    }
    if(file!=&tmp_demo)return -EINVAL;
    if(off>=4)return 0;
    if(len>4-off)len=4-off;
    if(len)memcpy(buf,value+off,(size_t)len);
    return (int64_t)len;
}
int tmp_chmod(struct nas_tmp_file *f,uint64_t mode){
    if(f!=&tmp_demo)return -EINVAL;
    f->mode=0100000U | ((uint32_t)mode&07777U);return 0;
}
int tmp_chown(struct nas_tmp_file *f,uint32_t uid,uint32_t gid){
    if(f!=&tmp_demo)return -EINVAL;
    if(uid!=UINT32_MAX)f->uid=uid;
    if(gid!=UINT32_MAX)f->gid=gid;
    return 0;
}
int tmp_utimens(struct nas_tmp_file *f,uint64_t a,uint64_t m,bool sa,bool sm){
    if(f!=&tmp_demo)return -EINVAL;
    if(sa)f->atime_ns=a;
    if(sm)f->mtime_ns=m;
    return 0;
}
bool nas_proc_lookup(const char *path,struct nas_proc_node *out){
    if(!strcmp(path,"/proc/self/fd")) {
        *out=(struct nas_proc_node){.kind=NAS_PROC_FD_DIR,.pid=1};
        return true;
    }
    if(!strcmp(path,"/proc/self/fd/3")) {
        *out=(struct nas_proc_node){.kind=NAS_PROC_FD_LINK,.pid=1,.fd=3};
        return true;
    }
    if(strcmp(path,"/proc") && strcmp(path,"/proc/self"))return false;
    *out=(struct nas_proc_node){.kind=NAS_PROC_ROOT};return true;
}
uint32_t nas_proc_mode(struct nas_proc_node node){
    return node.kind==NAS_PROC_FD_LINK ? 0120777U : 0040555U;
}
bool nas_proc_is_dir(struct nas_proc_node node){return node.kind!=NAS_PROC_FD_LINK;}
bool nas_proc_node_from_ino(uint64_t ino,struct nas_proc_node *out) {
    if(ino!=7000 || !out)return false;
    *out=(struct nas_proc_node){.kind=NAS_PROC_VERSION};
    return true;
}
int64_t nas_proc_read(struct nas_proc_node node,uint64_t off,
                      uint8_t *dst,unsigned length) {
    (void)node;
    static const char data[]="NaSeed proc fixture";
    if(off>=sizeof(data)-1)return 0;
    if(length>sizeof(data)-1-off)length=(unsigned)(sizeof(data)-1-off);
    if(length)memcpy(dst,data+off,length);
    return length;
}
bool nas_proc_fd_link_from_ino(uint64_t ino,struct nas_proc_node *out) {
    if(ino!=7000 || !out)return false;
    *out=(struct nas_proc_node){.kind=NAS_PROC_FD_LINK,.pid=1,.fd=3};
    return true;
}
int64_t nas_proc_readlink(struct nas_proc_node node,char *target,unsigned cap) {
    static const char name[]="pipe:[42]";
    if(node.kind!=NAS_PROC_FD_LINK || node.fd!=3)return -EINVAL;
    if(cap<sizeof(name)-1)return -ENAMETOOLONG;
    memcpy(target,name,sizeof(name)-1);
    return (int64_t)sizeof(name)-1;
}
struct nas_tmp_file *tmp_create(const char *path,bool dir,uint32_t mode) {
    if(strcmp(path,"/tmp/newfile") || dir || mode!=0640)return NULL;
    fixture_created_length=1;fixture_created.size=1;
    return &fixture_created;
}
int tmp_resize(struct nas_tmp_file *file,uint64_t size) {
    if(file!=&fixture_created || size>sizeof(fixture_created_bytes))return -EINVAL;
    fixture_created_length=(unsigned)size;file->size=size;return 0;
}
int tmp_preallocate(struct nas_tmp_file *file,uint64_t offset,
                    uint64_t length,unsigned mode) {
    if(file!=&fixture_created || mode || offset>UINT64_MAX-length)
        return -EINVAL;
    return tmp_resize(file,offset+length);
}
int64_t tmp_write_at(struct nas_tmp_file *file,uint64_t off,
                     const uint8_t *source,uint64_t count) {
    if(file!=&fixture_created || off+count>sizeof(fixture_created_bytes))
        return -EINVAL;
    memcpy(fixture_created_bytes+off,source,(size_t)count);
    fixture_created_length=(unsigned)(off+count);
    fixture_created.size=fixture_created_length;
    return (int64_t)count;
}
int tmp_mkdir(const char *path,uint64_t mode) {
    return !strcmp(path,"/tmp/new") && mode==0755 ? 0 : -EINVAL;
}
int tmp_unlink(const char *path,uint64_t flags) {
    if(!strcmp(path,"/tmp/m107-socket") && !flags &&
       socket_visible_index<4) {
        fixture_sockets[socket_visible_index].nlink=0;
        socket_visible_index=4;
        return 0;
    }
    if(!strcmp(path,"/tmp/to-detach") && flags==0x200U &&
       fixture_detached_dir_visible) {
        fixture_detached_dir_visible=false;
        fixture_detached_dir.nlink=0;
        return 0;
    }
    return !strcmp(path,"/tmp/demo") && flags==0 ? 0 : -EINVAL;
}
int tmp_rename(const char *oldpath,const char *newpath,uint64_t flags) {
    if(!strcmp(oldpath,fixture_dir_path) &&
       !strcmp(newpath,"/tmp/renamed-opened") && !flags) {
        strcpy(fixture_dir_path,newpath);
        strcpy(fixture_child_path,"/tmp/renamed-opened/child");
        return 0;
    }
    return !strcmp(oldpath,"/tmp/demo") && !strcmp(newpath,"/tmp/other") && flags==0 ? 0 : -EINVAL;
}
int tmp_link(const char *oldpath,const char *newpath) {
    return !strcmp(oldpath,"/tmp/demo") && !strcmp(newpath,"/tmp/linked") ? 0 : -EINVAL;
}
int tmp_symlink(const char *target,const char *linkpath) {
    return !strcmp(target,"../target") && !strcmp(linkpath,"/tmp/newlink") ? 0 : -EINVAL;
}

const struct bootfs_entry *bootfs_child(const struct bootfs_entry *dir,
                                        unsigned index) {
    if(dir==&entries[0]) {
        static const unsigned root_children[]={1,3,4,5,7};
        return index<sizeof(root_children)/sizeof(root_children[0])
            ? &entries[root_children[index]]:NULL;
    }
    if(dir==&entries[1])return index==0?&entries[2]:NULL;
    if(dir==&entries[3])return index==0?&entries[6]:NULL;
    return NULL;
}
uint64_t tmp_number(const struct nas_tmp_file *file) {return file->ino;}
struct nas_tmp_file *tmp_find_child(const void *parent,const char *name) {
    const struct bootfs_entry *root=bootfs_find("/tmp");
    char path[256];
    if(parent==(const void *)root)strcpy(path,"/tmp");
    else {
        const char *base=tmp_name(parent);
        if(!base)return NULL;
        strcpy(path,base);
    }
    size_t n=strlen(path),m=strlen(name);
    if(n+m+1>=sizeof(path))return NULL;
    path[n++]='/';memcpy(path+n,name,m+1);
    return tmp_find(path);
}
static const struct nas_tmp_dentry demo_dentry={
    .used=true,.parent_inode=&entries[3],.name="demo",.inode=&tmp_demo
};
static const struct nas_tmp_dentry link_dentry={
    .used=true,.parent_inode=&entries[3],.name="link",.inode=&fixture_symlink
};
static struct nas_tmp_dentry nested_dentry={
    .used=true,.parent_inode=&fixture_open_directory,.name="child",.inode=&fixture_open_child
};
const struct nas_tmp_dentry *tmp_child_entry(const char *path,unsigned index) {
    if(!strcmp(path,fixture_dir_path)) {
        return index==0?&nested_dentry:NULL;
    }
    if(strcmp(path,"/tmp"))return NULL;
    return index==0?&demo_dentry:index==1?&link_dentry:NULL;
}
/* Host mock models the new VFS contract: the actual provider receives
 * the parent inode and basename. Reconstruct mock paths only to address
 * its fixed test catalog, never inside production provider callbacks. */
static int mock_tmp_child_path(const void *parent,const char *name,
                               char out[256]) {
    const char *base=parent==(const void *)&entries[3] ?
                      "/tmp":tmp_name(parent);
    if(!base)return -ESTALE;
    return nas_vfs_join_child(base,name,out);
}
const struct nas_tmp_dentry *tmp_child_entry_inode(const void *parent,unsigned index) {
    if(parent==(const void *)&entries[3])return tmp_child_entry("/tmp",index);
    if(parent==(const void *)&fixture_open_directory)
        return tmp_child_entry(fixture_dir_path,index);
    return NULL;
}
const void *tmp_dir_parent_inode(const void *dir) {
    if(dir==(const void *)&entries[3])return dir;
    if(dir==(const void *)&fixture_open_directory)return &entries[3];
    if(dir==(const void *)&fixture_detached_dir)return &entries[3];
    return NULL;
}
struct nas_tmp_file *tmp_create_at(const void *parent,const char *name,
                                    bool directory,uint32_t mode) {
    char path[256];
    return mock_tmp_child_path(parent,name,path) ? NULL :
           tmp_create(path,directory,mode);
}
struct nas_tmp_file *tmp_mknod_at(const void *parent,const char *name,
                                  uint32_t mode) {
    if(parent!=(const void *)&entries[3] ||
       strcmp(name,"m107-socket") || socket_visible_index<4 ||
       (mode&0170000U)!=0140000U)return NULL;
    for(unsigned i=0;i<4;i++) {
        if(fixture_sockets[i].used)continue;
        struct nas_tmp_file *node=&fixture_sockets[i];
        *node=(struct nas_tmp_file){.used=true,.nlink=1,.mode=mode,
                                    .ino=20000U + ++socket_generation};
        socket_visible_index=i;
        return node;
    }
    return NULL;
}
int tmp_mkdir_at(const void *parent,const char *name,uint64_t mode) {
    char path[256];int rc=mock_tmp_child_path(parent,name,path);
    return rc ? rc : tmp_mkdir(path,mode);
}
int tmp_unlink_at(const void *parent,const char *name,uint64_t flags) {
    char path[256];int rc=mock_tmp_child_path(parent,name,path);
    return rc ? rc : tmp_unlink(path,flags);
}
int tmp_rename_at(const void *old_parent,const char *old_name,
                  const void *new_parent,const char *new_name,uint64_t flags) {
    char from[256],to[256];
    int rc=mock_tmp_child_path(old_parent,old_name,from);
    if(rc)return rc;
    rc=mock_tmp_child_path(new_parent,new_name,to);
    return rc ? rc : tmp_rename(from,to,flags);
}
int tmp_link_at(const void *old_parent,const char *old_name,
                const void *new_parent,const char *new_name) {
    char from[256],to[256];
    int rc=mock_tmp_child_path(old_parent,old_name,from);
    if(rc)return rc;
    rc=mock_tmp_child_path(new_parent,new_name,to);
    return rc ? rc : tmp_link(from,to);
}
int tmp_symlink_at(const void *parent,const char *name,const char *target) {
    char path[256];int rc=mock_tmp_child_path(parent,name,path);
    return rc ? rc : tmp_symlink(target,path);
}
uint64_t nas_proc_ino(struct nas_proc_node p) {(void)p;return 7000;}
bool nas_proc_dir_path(struct nas_proc_node node,char path[256]) {
    if(node.kind!=NAS_PROC_ROOT)return false;
    memcpy(path,"/proc",6);
    return true;
}
bool nas_proc_directory_from_ino(uint64_t ino,struct nas_proc_node *out) {
    if(ino!=7000 || !out)return false;
    *out=(struct nas_proc_node){.kind=NAS_PROC_ROOT};
    return true;
}
bool nas_proc_find_child(uint64_t ino,const char *name,struct nas_proc_node *out) {
    (void)ino; /* The fixture uses a single ID for its synthetic nodes. */
    if(!strcmp(name,"self"))return nas_proc_lookup("/proc/self",out);
    if(!strcmp(name,"fd"))return nas_proc_lookup("/proc/self/fd",out);
    if(!strcmp(name,"3"))return nas_proc_lookup("/proc/self/fd/3",out);
    return false;
}
bool nas_proc_child(struct nas_proc_node directory,unsigned index,
                    char name[32],struct nas_proc_node *child) {
    (void)directory;
    if(index)return false;
    memcpy(name,"self",5);
    *child=(struct nas_proc_node){.kind=NAS_PROC_ROOT};
    return true;
}

static unsigned mounted_lookups;
static uint64_t ext_mock_root_ino=801;
static int ext_mock_lookup(const struct nas_vfs_mount *mount,
                           const char *path,struct nas_fs_node *node){
    (void)mount;
    mounted_lookups++;
    if(strcmp(path,"/mnt/hello") && strcmp(path,"/mnt"))return -ENOENT;
    *node=!strcmp(path,"/mnt")
        ? (struct nas_fs_node){.ino=ext_mock_root_ino,.mode=0040755U,.is_dir=true}
        : (struct nas_fs_node){.ino=802,.mode=0100644U,.size=4};
    return 0;
}
static int ext_mock_root(const struct nas_vfs_mount *m,
                         struct nas_fs_node *node) {
    return ext_mock_lookup(m,"/mnt",node);
}
static int ext_mock_child(const struct nas_fs_node *parent,const char *name,
                          struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    return !strcmp(name,"hello") ?
        ext_mock_lookup(parent->mount,"/mnt/hello",out) : -ENOENT;
}
static const struct nas_vfs_type ext_mock_type={
    .name="ext-mount-mock",.root=ext_mock_root,
    .lookup_child=ext_mock_child,.read_only=true
};
static const struct nas_vfs_mount ext_mount={
    .mountpoint="/mnt",.type=&ext_mock_type
};
/* A host-only syscall-shaped adapter. It calls production namei and
 * production child mutators with Path ownership; no Linux host filesystem
 * operations are used for the expected NaSeed result. */
static int namespace_unlink(const char *path,uint64_t flags) {
    struct nas_vfs_path parent={0};char leaf[256];
    int rc=nas_fs_lookup_parent_at(NULL,NULL,path,&parent,leaf);
    if(!rc)rc=nas_vfs_unlink_child(&parent,leaf,flags);
    if(parent.dentry)assert(nas_vfs_path_put(&parent)==0);
    return rc;
}
static int namespace_rename(const char *old_name,const char *new_name) {
    struct nas_vfs_path before={0},after={0};
    char old_leaf[256],new_leaf[256];
    int rc=nas_fs_lookup_parent_at(NULL,NULL,old_name,&before,old_leaf);
    if(!rc)rc=nas_fs_lookup_parent_at_policy(
               NULL,NULL,new_name,NAS_FS_TRAILING_RENAME_DEST,
               &after,new_leaf);
    size_t length=strlen(new_name);
    bool new_trailing=length>1 && new_name[length-1]=='/';
    if(!rc)rc=nas_vfs_rename_child_with_intent(
               &before,old_leaf,&after,new_leaf,0,new_trailing);
    if(after.dentry)assert(nas_vfs_path_put(&after)==0);
    if(before.dentry)assert(nas_vfs_path_put(&before)==0);
    return rc;
}
static int namespace_link(const char *old_name,const char *new_name) {
    struct nas_vfs_path before={0},after={0};
    char new_leaf[256];
    int rc=nas_fs_lookup_path_at(NULL,NULL,old_name,false,&before);
    if(!rc)rc=nas_fs_lookup_parent_at_policy(
               NULL,NULL,new_name,NAS_FS_TRAILING_CREATE_DEST,
               &after,new_leaf);
    if(!rc)rc=nas_vfs_link_path_child(&before,&after,new_leaf);
    if(after.dentry)assert(nas_vfs_path_put(&after)==0);
    if(before.dentry)assert(nas_vfs_path_put(&before)==0);
    return rc;
}
static int namespace_symlink(const char *target,const char *new_name) {
    struct nas_vfs_path parent={0};char leaf[256];
    int rc=nas_fs_lookup_parent_at_policy(
               NULL,NULL,new_name,NAS_FS_TRAILING_CREATE_DEST,
               &parent,leaf);
    if(!rc)rc=nas_vfs_symlink_child(target,&parent,leaf);
    if(parent.dentry)assert(nas_vfs_path_put(&parent)==0);
    return rc;
}
static int namespace_mkdir_at(const struct nas_vfs_path *start,
                              const char *pathname) {
    /* Same root-only dispatch + production Path namei/child mutation
     * sequence as sys_mkdirat_tmp, without a forged syscall return. */
    bool root_only=pathname[0]=='/';
    for(unsigned i=1;root_only && pathname[i];i++)
        if(pathname[i]!='/')root_only=false;
    if(root_only)return -EEXIST;
    struct nas_vfs_path parent={0};char leaf[256];
    int rc=nas_fs_lookup_parent_mkdir_at(start,NULL,pathname,
                                         &parent,leaf);
    if(!rc)rc=nas_vfs_mkdir_child(&parent,leaf,0755);
    if(parent.dentry)assert(nas_vfs_path_put(&parent)==0);
    return rc;
}
static int namespace_create(const char *path,uint64_t mode) {
    /* Creation tests must examine the final name itself. Using the O_CREAT
     * dangling-link resolver here would incorrectly follow the last link. */
    struct nas_vfs_path parent={0};char leaf[256];
    int rc=nas_fs_lookup_parent_at(NULL,NULL,path,&parent,leaf);
    if(!rc)rc=nas_vfs_create_child(&parent,leaf,mode);
    if(parent.dentry)assert(nas_vfs_path_put(&parent)==0);
    return rc;
}
static void assert_create_parent(const char *path,const char *base,
                                 const char *expected_leaf,int expected) {
    struct nas_vfs_path parent={0};
    char leaf[256],resolved[256];
    int rc=nas_fs_resolve_create_parent_at(NULL,NULL,path,&parent,leaf);
    assert(rc==expected);
    if(!rc) {
        assert(nas_vfs_path_string(&parent,resolved)==0);
        assert(!strcmp(resolved,base) && !strcmp(leaf,expected_leaf));
    }
    if(parent.dentry)assert(nas_vfs_path_put(&parent)==0);
}
static unsigned mutation_parity_errors;
static void mutation_parity(const char *op,const char *source,
                            const char *target,int expected,
                            int owned,int legacy) {
    if(owned!=expected || legacy!=owned) {
        fprintf(stderr,"[VFS LEGACY PARITY FAIL] %s(%s,%s): "
                       "expected=%d path-first=%d legacy=%d\n",
                op,source,target?target:"",expected,owned,legacy);
        ++mutation_parity_errors;
    }
}
/* Public absolute-path VFS APIs remain necessary for upstream FS/early
 * boot callers, but they MUST be wrappers around the SAME owned Path-first
 * mutation algorithms as Linux *at syscalls. Keep these comparisons on real
 * production namei + VFS mutators, never separately modeled mock outcomes.
 * All test cases are read-only, no-op, or the fixture's intentionally
 * unmaterialized provider callbacks; other later tests keep their state. */
static void test_legacy_owned_mutation_parity(void) {
    struct {const char *path;int expected;} create_cases[]={
        {"/tmp/demo",-EEXIST},{"/tmp/newfile",-EEXIST},
        {"/bin/new",-EROFS},{"/tmp/demo/child",-ENOTDIR}
    };
    for(unsigned i=0;i<sizeof(create_cases)/sizeof(create_cases[0]);i++) {
        int owned=namespace_create(create_cases[i].path,0640);
        int legacy=nas_vfs_create(create_cases[i].path,0640);
        mutation_parity("create",create_cases[i].path,NULL,
                        create_cases[i].expected,owned,legacy);
    }
    const char *saved_target=fixture_link_target;
    unsigned saved_length=(unsigned)fixture_symlink.size;
    fixture_link_target="no-such-target";
    fixture_symlink.size=14;
    /* A dangling FINAL symlink still occupies the name: plain create and
     * O_CREAT|O_EXCL both return EEXIST; nonexclusive open differs. */
    assert(namespace_create("/tmp/link",0640)==-EEXIST);
    assert(nas_vfs_create("/tmp/link",0640)==-EEXIST);
    fixture_link_target=saved_target;
    fixture_symlink.size=saved_length;

    struct {const char *path;int expected;} mkdir_cases[]={
        {"/",-EEXIST},{"/tmp/",-EEXIST},
        {"/tmp/demo/",-EEXIST},{"/tmp/alias/",-EEXIST},
        {"/tmp/.",-EEXIST},{"/tmp/..",-EEXIST},
        {"/tmp/missing/.",-ENOENT},{"/tmp/demo/.",-ENOTDIR},
        {"/bin/new",-EROFS}
    };
    for(unsigned i=0;i<sizeof(mkdir_cases)/sizeof(mkdir_cases[0]);i++) {
        int owned=namespace_mkdir_at(NULL,mkdir_cases[i].path);
        int legacy=nas_vfs_mkdir(mkdir_cases[i].path,0755);
        mutation_parity("mkdir",mkdir_cases[i].path,NULL,
                        mkdir_cases[i].expected,owned,legacy);
    }
    struct {const char *path;uint64_t flags;int expected;} unlink_cases[]={
        {"/tmp/demo/",0,-ENOTDIR},{"/tmp/alias/",0,-ENOTDIR},
        {"/tmp/demo",0x800U,-EINVAL},{"/bin/demo",0,-EROFS},
        {"/tmp/missing",0,-ENOENT}
    };
    for(unsigned i=0;i<sizeof(unlink_cases)/sizeof(unlink_cases[0]);i++) {
        int owned=namespace_unlink(unlink_cases[i].path,unlink_cases[i].flags);
        int legacy=nas_vfs_unlink(unlink_cases[i].path,unlink_cases[i].flags);
        mutation_parity("unlink",unlink_cases[i].path,NULL,
                        unlink_cases[i].expected,owned,legacy);
    }
    struct {const char *source,*target;int expected;} rename_cases[]={
        {"/tmp/demo/","/tmp/other",-ENOTDIR},
        {"/tmp/demo","/tmp/no-such/",-ENOTDIR},
        {"/tmp/demo","/tmp/renamed-opened/",-ENOTDIR},
        {"/tmp/demo","/tmp/demo",0},
        {"/tmp/hardlink-a","/tmp/hardlink-b",0},
        {"/tmp/demo","/bin/demo",-EXDEV}
    };
    for(unsigned i=0;i<sizeof(rename_cases)/sizeof(rename_cases[0]);i++) {
        int owned=namespace_rename(rename_cases[i].source,rename_cases[i].target);
        int legacy=nas_vfs_rename(rename_cases[i].source,rename_cases[i].target,0);
        mutation_parity("rename",rename_cases[i].source,rename_cases[i].target,
                        rename_cases[i].expected,owned,legacy);
    }
    /* A public legacy rename with an already-held exact Dentry must not
     * rebind or destroy that name owner when the operation is a no-op. */
    struct nas_vfs_path pinned_noop={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,"/tmp/renamed-opened",false,
                                 &pinned_noop)==0);
    assert(nas_vfs_rename("/tmp/renamed-opened","/tmp/renamed-opened",0)==0);
    struct nas_fs_node no_op_inode={0};
    char still_owned[256];
    assert(nas_vfs_path_node(&pinned_noop,&no_op_inode)==0);
    assert(no_op_inode.inode==&fixture_open_directory);
    assert(nas_vfs_path_string(&pinned_noop,still_owned)==0);
    assert(!strcmp(still_owned,"/tmp/renamed-opened"));
    assert(nas_vfs_path_put(&pinned_noop)==0);
    struct {const char *source,*target;int expected;} link_cases[]={
        {"/tmp","/tmp/direct",-EPERM},
        {"/bin/demo","/tmp/copy",-EXDEV},
        {"/tmp/demo","/tmp/demo",-EEXIST},
        {"/tmp/demo","/tmp/demo/",-EEXIST},
        {"/tmp/demo/","/tmp/newname",-ENOTDIR}
    };
    for(unsigned i=0;i<sizeof(link_cases)/sizeof(link_cases[0]);i++) {
        int owned=namespace_link(link_cases[i].source,link_cases[i].target);
        int legacy=nas_vfs_link(link_cases[i].source,link_cases[i].target);
        mutation_parity("link",link_cases[i].source,link_cases[i].target,
                        link_cases[i].expected,owned,legacy);
    }
    struct {const char *target,*path;int expected;} symlink_cases[]={
        {"anything","/tmp/demo/",-EEXIST},
        {"anything","/tmp/alias/",-EEXIST},
        {"anything","/tmp/missing-symlink/",-ENOENT},
        {"anything","/bin/newlink",-EROFS}
    };
    for(unsigned i=0;i<sizeof(symlink_cases)/sizeof(symlink_cases[0]);i++) {
        int owned=namespace_symlink(symlink_cases[i].target,symlink_cases[i].…9527 tokens truncated…an;
    assert(nas_fs_lookup_node("/tmp/pinned-orphan",true,&orphan,resolved)==0);
    assert(orphan.inode==&fixture_orphan);
    assert(nas_vfs_node_pin(&orphan)==0);
    assert(nas_vfs_inode_is_pinned(&fixture_orphan));
    /* Open File keeps its inode even after unlink. Refresh st_nlink=0. */
    fixture_orphan.nlink=0; /* simulate a successful backend unlink AFTER pin */
    struct nas_fs_node refreshed,forged=orphan;
    orphan.nlink=1; /* obsolete pathname snapshot */
    assert(nas_vfs_getattr(&orphan,&refreshed)==0);
    assert(refreshed.nlink==0 && refreshed.inode==orphan.inode);
    /* Reusing the inode number with a different backend identity must not
     * release the live owner or publish stale metadata. */
    forged.inode=&fixture_open_directory;
    assert(nas_vfs_node_pin(&forged)==-ESTALE);
    assert(nas_vfs_node_unpin(&forged)==-ESTALE);
    assert(nas_vfs_getattr(&forged,&refreshed)==-ESTALE);
    /* No data, metadata or durability provider may run against a stale
     * incarnation while a different open File holds the real inode. */
    assert(nas_vfs_read_at(&forged,0,(uint8_t *)data,1)==-ESTALE);
    assert(nas_vfs_write_at(&forged,0,(const uint8_t *)"x",1)==-ESTALE);
    assert(nas_vfs_resize(&forged,1)==-ESTALE);
    assert(nas_vfs_sync(&forged)==-ESTALE);
    assert(nas_vfs_chmod(&forged,0600)==-ESTALE);
    assert(nas_vfs_chown(&forged,123,456)==-ESTALE);
    assert(nas_vfs_utimens(&forged,0,0,true,true)==-ESTALE);
    assert(nas_vfs_inode_is_pinned(&fixture_orphan));
    assert(nas_vfs_node_pin(&orphan)==0); /* separate cwd and shared File */
    assert(nas_vfs_node_unpin(&orphan)==0);
    assert(orphan_releases==0 && fixture_orphan.used);
    assert(nas_vfs_node_unpin(&orphan)==0);
    assert(orphan_releases==1 && !fixture_orphan.used);
    assert(!nas_vfs_inode_is_pinned(&fixture_orphan));
    assert(nas_vfs_sync(&n)==0);
    assert(nas_vfs_create("/bin/new",0644)==-EROFS);
    assert(nas_vfs_resize(&n,3)==0);
    assert(nas_vfs_mkdir("/tmp/new",0755)==0);
    assert(nas_vfs_unlink("/tmp/demo",0)==0);
    assert(nas_vfs_rename("/tmp/demo","/tmp/other",0)==0);
    assert(nas_vfs_link("/tmp/demo","/tmp/linked")==0);
    assert(nas_vfs_symlink("../target","/tmp/newlink")==0);
    assert(nas_vfs_mkdir("/",0755)==-EEXIST);
    /* Linux treats any slash-only spelling as the existing mount root.
     * Creation/linking reports EEXIST; unlink differs from rmdir. These
     * are real namespace policy cases, not provider special cases. */
    assert(nas_fs_root_only("/") && nas_fs_root_only("////"));
    assert(!nas_fs_root_only("/tmp/") && !nas_fs_root_only("tmp"));
    assert(nas_vfs_create("////",0644)==-EEXIST);
    assert(nas_vfs_mkdir("////",0755)==-EEXIST);
    assert(nas_vfs_symlink("target","////")==-EEXIST);
    assert(nas_vfs_link("/bin/demo","////")==-EEXIST);
    assert(nas_vfs_unlink("////",0)==-EISDIR);
    assert(nas_vfs_unlink("////",0x200ULL)==-EBUSY);
    struct nas_vfs_path impossible_parent={0};
    char impossible_leaf[256];
    assert(nas_fs_lookup_parent_mkdir_at(NULL,NULL,"////",
                &impossible_parent,impossible_leaf)==-EEXIST);
    assert(nas_fs_lookup_parent_at_policy(NULL,NULL,"////",
                NAS_FS_TRAILING_CREATE_DEST,&impossible_parent,
                impossible_leaf)==-EEXIST);
    assert(!impossible_parent.dentry);
    assert(nas_vfs_mkdir("/tmp",0755)==-EEXIST);
    assert(nas_vfs_mkdir("/bin",0755)==-EEXIST);
    assert(nas_vfs_mkdir("/tmp/demo",0755)==-EEXIST);
    assert(nas_vfs_mkdir("/tmp/alias/new",0755)==0);
    assert(nas_vfs_unlink("/tmp/alias/demo",0)==0);
    assert(nas_vfs_rename("/tmp/alias/demo","/tmp/alias/other",0)==0);
    assert(nas_vfs_link("/tmp/alias/demo","/tmp/alias/linked")==0);
    assert(nas_vfs_symlink("../target","/tmp/alias/newlink")==0);
    assert(nas_vfs_mkdir("/bin/new",0755)==-EROFS);
    assert(nas_vfs_unlink("/bin/demo",0)==-EROFS);
    assert(nas_vfs_rename("/tmp/demo","/bin/other",0)==-EXDEV);
    assert(nas_vfs_link("/bin/demo","/tmp/copy")==-EXDEV);
    assert(nas_vfs_mkdir("/tmp/demo/sub",0755)==-ENOTDIR);
    test_legacy_owned_mutation_parity();
    assert(nas_fs_lookup_node("/shortcut",false,&n,resolved)==0 && n.is_symlink);
    /* Direct kernel lookup and syscall namei must now share ONE pathwalk:
     * inspect a final symlink itself; follow an intermediate symlink;
     * never bypass a regular-file dot/dotdot parent. */
    assert(nas_vfs_lookup("/shortcut",&direct)==0 && direct.is_symlink);
    assert(nas_vfs_lookup("/bin/demo/..",&direct)==-ENOTDIR);
    assert(nas_vfs_lookup("/bin/demo/../demo",&direct)==-ENOTDIR);
    assert(nas_vfs_lookup("/tmp/../bin/demo",&direct)==0);
    assert(!strcmp(direct.filesystem->name,"bootfs"));
    /* A long series of ancestor steps MUST restart the same walker,
     * never recurse through direct VFS lookup and overflow the 16KiB
     * kernel stack. This also checks the rewritten pending path length. */
    char many_parents[256]="/tmp";
    for(unsigned turn=0;turn<24;turn++)strcat(many_parents,"/../tmp");
    strcat(many_parents,"/../bin/demo");
    assert(strlen(many_parents)<256);
    assert(nas_vfs_lookup(many_parents,&direct)==0);
    assert(direct.ino==3);
    assert(nas_vfs_lookup("/shortcut/../bin/demo",&direct)==-ENOTDIR);
    assert(nas_fs_lookup_node("/shortcut",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo"));
    assert(nas_fs_lookup_node("/tmp/link",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo"));
    assert(nas_fs_lookup_node("/tmp/../bin/demo",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo"));
    assert(nas_fs_lookup_node("/bin/demo/no",true,&n,resolved)==-ENOTDIR);
    /* Dot/dotdot are not lexical shortcuts around a non-directory inode.
     * No provider may get a successful path by removing these components. */
    assert(nas_fs_lookup_node("/bin/demo/.",true,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/bin/demo/..",true,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/bin/demo/../demo",true,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/bin/demo/./no",true,&n,resolved)==-ENOTDIR);
    assert(nas_fs_resolve_existing("/bin/demo/..",true,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/bin/./demo",true,&n,resolved)==0);
    assert(nas_fs_lookup_node("/bin/../bin/demo",true,&n,resolved)==0);
    assert(nas_fs_lookup_node("/bin/demo/",true,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/bin/demo///",false,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/shortcut/",false,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/tmp/link/",false,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/bin/",false,&n,resolved)==0 && n.is_dir);
    assert(nas_fs_lookup_node("/missing",true,&n,resolved)==-ENOENT);
    struct nas_vfs_mount bad_mount=ext_mount;
    bad_mount.mountpoint="/mnt//x";
    assert(nas_vfs_register_mount(&bad_mount)==-EINVAL);
    bad_mount.mountpoint="/mnt/./x";
    assert(nas_vfs_register_mount(&bad_mount)==-EINVAL);
    bad_mount.mountpoint="/mnt/../x";
    assert(nas_vfs_register_mount(&bad_mount)==-EINVAL);
    bad_mount.mountpoint="/mnt/x/";
    assert(nas_vfs_register_mount(&bad_mount)==-EINVAL);
    /* An exact mountpoint may not cover a regular file. Missing nested
     * parents may not be manufactured by mount-prefix dispatch. */
    bad_mount.mountpoint="/bin/demo";
    assert(nas_vfs_register_mount(&bad_mount)==-ENOTDIR);
    bad_mount.mountpoint="/missing/child";
    assert(nas_vfs_register_mount(&bad_mount)==-ENOENT);
    /* Ordinary mounts cannot invent an absent covered dentry. An
     * explicitly marked bootstrap can, and remains detachable. */
    bad_mount.mountpoint="/tmp/boot-only-mount";
    assert(nas_vfs_register_mount(&bad_mount)==-ENOENT);
    bad_mount.allow_missing_mountpoint=true;
    assert(nas_vfs_register_mount(&bad_mount)==0);
    assert(nas_fs_lookup_node("/tmp/boot-only-mount",true,&n,resolved)==0);
    assert(n.filesystem==&ext_mock_type);
    struct nas_fs_node virtual_parent;
    struct nas_vfs_dirent virtual_entries[8];
    assert(nas_fs_lookup_node("/tmp",true,&virtual_parent,resolved)==0);
    /* No tmpfs-specific code adds this entry: the mounted VFS parent
     * exposes a virtual child followed by ordinary backend children. */
    assert(nas_vfs_read_dir(&virtual_parent,0,virtual_entries,8)==5);
    assert(!strcmp(virtual_entries[0].name,"."));
    assert(!strcmp(virtual_entries[1].name,".."));
    assert(!strcmp(virtual_entries[2].name,"boot-only-mount"));
    assert(virtual_entries[2].ino==801 && virtual_entries[2].dtype==4);
    assert(!strcmp(virtual_entries[3].name,"demo"));
    assert(nas_vfs_read_dir(&virtual_parent,2,virtual_entries,1)==1);
    assert(!strcmp(virtual_entries[0].name,"boot-only-mount"));
    assert(nas_vfs_read_dir(&virtual_parent,3,virtual_entries,2)==2);
    assert(!strcmp(virtual_entries[0].name,"demo"));
    assert(nas_vfs_read_dir(&virtual_parent,5,virtual_entries,8)==0);
    assert(nas_vfs_unregister_mount(&bad_mount)==0);
    assert(nas_vfs_read_dir(&virtual_parent,0,virtual_entries,8)==4);
    assert(!strcmp(virtual_entries[2].name,"demo"));
    bad_mount.allow_missing_mountpoint=false;
    bad_mount.mountpoint="/bin/demo/child";
    assert(nas_vfs_register_mount(&bad_mount)==-ENOTDIR);
    /* Linux mount attachment has a covered parent dentry, not an alias
     * spelled by the caller. An unresolved symlink mount path must not
     * attach a second location under the same parent inode. */
    bad_mount.mountpoint="/tmp/alias/mounted";
    assert(nas_vfs_register_mount(&bad_mount)==-EINVAL);
    /* A symlink at the FINAL covered mountpoint is not a mountable
     * directory identity either: following /tmp/alias lands at /tmp. */
    bad_mount.mountpoint="/tmp/alias";
    assert(nas_vfs_register_mount(&bad_mount)==-EINVAL);
    /* The old cwd/dirfd path remains correct on an ordinary directory. */
    assert(nas_fs_lookup_node("/bin/.",true,&n,resolved)==0 &&
           !strcmp(resolved,"/bin"));
    assert(nas_fs_lookup_node("/bin/..",true,&n,resolved)==0 &&
           !strcmp(resolved,"/"));
    assert(!nas_vfs_inode_is_pinned(&entries[5]));
    /* A real mount COVER and the VISIBLE mounted root are separate
     * dentries occupying the same parent/name in different mount layers.
     * They must NOT detach one another during normal Path acquisition. */
    struct nas_fs_node underlay_node;
    char underlay_name[256];
    assert(nas_fs_lookup_node("/mnt",true,
                              &underlay_node,underlay_name)==0);
    struct nas_vfs_path underlay_path={0},visible_path={0};
    assert(nas_vfs_path_bind(&underlay_node,"/mnt",&underlay_path)==0);
    assert(nas_vfs_register_mount(&ext_mount)==0);
    test_legacy_mounted_parity();
    assert(mutation_parity_errors==0);
    struct nas_fs_node visible_node;
    assert(nas_fs_lookup_node("/mnt",true,&visible_node,resolved)==0);
    assert(visible_node.mount!=underlay_node.mount);
    assert(nas_vfs_path_bind(&visible_node,"/mnt",&visible_path)==0);
    char mount_path_name[256];
    assert(nas_vfs_path_string(&underlay_path,mount_path_name)==0);
    assert(!strcmp(mount_path_name,"/mnt"));
    assert(nas_vfs_path_string(&visible_path,mount_path_name)==0);
    assert(!strcmp(mount_path_name,"/mnt"));
    assert(visible_path.dentry!=underlay_path.dentry);
    assert(nas_vfs_unregister_mount(&ext_mount)==-EBUSY);
    assert(nas_vfs_path_put(&visible_path)==0);
    assert(nas_vfs_path_put(&underlay_path)==0);
    /* Covered readdir names must report the visible mounted root inode,
     * not the hidden bootfs /mnt directory's original inode (6). */
    struct nas_fs_node root_list;
    struct nas_vfs_dirent root_entries[8];
    assert(nas_fs_lookup_node("/",true,&root_list,resolved)==0);
    assert(nas_vfs_read_dir(&root_list,0,root_entries,8)==8);
    assert(!strcmp(root_entries[6].name,"mnt"));
    assert(root_entries[6].ino==801 && root_entries[6].dtype==4);
    /* The mount itself owns the covered /mnt directory and root parent.
     * A directory lease cannot evaporate merely because no user FD is open. */
    assert(nas_vfs_inode_is_pinned(&entries[5]));
    assert(nas_vfs_mkdir("/mnt",0755)==-EEXIST);
    assert(nas_vfs_unlink("/mnt",0x200ULL)==-EBUSY);
    assert(nas_vfs_unlink("/mnt",0x800ULL)==-EINVAL);
    assert(nas_vfs_rename("/mnt","/tmp/renamed",0)==-EBUSY);
    assert(nas_vfs_rename("/tmp/demo","/mnt",0)==-EBUSY);
    /* Direct production child mutation API must respect a mounted child
     * before provider mutation; no accidental removal of its underlay. */
    struct nas_vfs_path root_parent={0},mount_tmp_parent={0};
    assert(nas_vfs_path_root(&root_parent)==0);
    assert(nas_fs_lookup_path_at(NULL,NULL,"/tmp",true,
                                 &mount_tmp_parent)==0);
    assert(nas_vfs_unlink_child(&root_parent,"mnt",0x200ULL)==-EBUSY);
    assert(nas_vfs_rename_child(&root_parent,"mnt",
                                &mount_tmp_parent,"mnt-moved",0)==-EBUSY);
    assert(nas_vfs_rename_child(&mount_tmp_parent,"demo",
                                &root_parent,"mnt",0)==-EBUSY);
    assert(nas_vfs_link_child(&mount_tmp_parent,"demo",
                              &root_parent,"hardlink")==-EXDEV);
    assert(nas_vfs_path_put(&mount_tmp_parent)==0);
    assert(nas_vfs_path_put(&root_parent)==0);
    assert(nas_fs_lookup_node("/mnt/hello",true,&n,resolved)==0);
    assert(n.filesystem==&ext_mock_type);
    fprintf(stderr,"[PATH FIRST HOST MOUNT PASS] child unlink/rename/link busy and EXDEV\n");
    assert(nas_vfs_link("/tmp","/tmp/dir-alias")==-EPERM);
    assert(nas_vfs_register_mount(&ext_mount)==-EEXIST);
    assert(nas_fs_lookup_node("/mnt/hello",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"ext-mount-mock"));
    assert(mounted_lookups!=0);
    assert(nas_fs_lookup_node("/mnt/no",true,&n,resolved)==-ENOENT);
    assert(nas_fs_lookup_node("/mnt2",true,&n,resolved)==-ENOENT);
    assert(nas_fs_lookup_node("/mnt/hello",true,&n,resolved)==0);
    assert(nas_vfs_node_device(&n)>=8);
    assert(nas_vfs_node_pin(&n)==0);
    assert(nas_vfs_node_pin(&n)==0);
    assert(nas_vfs_unregister_mount(&ext_mount)==-EBUSY);
    assert(nas_vfs_node_unpin(&n)==0);
    assert(nas_vfs_unregister_mount(&ext_mount)==-EBUSY);
    assert(nas_vfs_node_unpin(&n)==0);
    struct nas_fs_node old_mount_incarnation=n;
    assert(nas_vfs_unregister_mount(&ext_mount)==0);
    assert(!nas_vfs_inode_is_pinned(&entries[5]));
    /* A previously borrowed inode cannot access its detached provider,
     * even when the provider's descriptor memory remains addressable. */
    assert(nas_vfs_read_at(&n,0,(uint8_t *)data,1)==-ENODEV);
    assert(nas_vfs_sync(&n)==-ENODEV);
    assert(nas_fs_lookup_node("/mnt/hello",true,&n,resolved)==-ENOENT);
    assert(nas_vfs_unregister_mount(&ext_mount)==-ENOENT);
    /* One provider can appear beneath two independent VFS mount records.
     * Directory lookup must never depend on its globally spelled mount path. */
    assert(nas_vfs_register_mount(&ext_mount)==0);
    /* Reusing the identical mount descriptor must NEVER resurrect a
     * borrowed inode from the previous mounted device incarnation. */
    assert(nas_vfs_node_pin(&old_mount_incarnation)==-ESTALE);
    assert(nas_vfs_read_at(&old_mount_incarnation,
                           0,(uint8_t *)data,1)==-ESTALE);
    assert(nas_vfs_node_device(&old_mount_incarnation)==0);
    static char alias_path[32]="/mnt-alias";
    static struct nas_vfs_mount alias_mount={
        .mountpoint=alias_path,.type=&ext_mock_type,
        .allow_missing_mountpoint=true
    };
    assert(nas_vfs_register_mount(&alias_mount)==0);
    /* Registration copies an OWNED superblock and pathname. Mutation of
     * the caller's old mount descriptor cannot retarget an attached
     * filesystem, change its backend or trick unmount path checks. */
    alias_mount.mountpoint="/wrong-reused-descriptor";
    assert(nas_vfs_register_mount(&alias_mount)==-EEXIST);
    alias_mount.type=NULL;
    memcpy(alias_path,"/wrong-stale",13);
    struct nas_fs_node first,second;
    assert(nas_vfs_lookup("/mnt/hello",&first)==0);
    assert(nas_vfs_lookup("/mnt-alias/hello",&second)==0);
    assert(first.ino==second.ino && first.mount!=second.mount);
    /* The real, shared namei pathwalk must obey exactly the same
     * mount-root/child contract as the direct kernel VFS lookup. */
    assert(nas_fs_lookup_node("/mnt-alias/hello",true,&second,resolved)==0);
    assert(second.mount!=&alias_mount && second.ino==first.ino);
    assert(!strcmp(second.mount->mountpoint,"/mnt-alias"));
    assert(second.filesystem==&ext_mock_type);
    assert(nas_fs_lookup_node("/wrong-stale/hello",true,
                              &n,resolved)==-ENOENT);
    assert(nas_fs_lookup_node("/wrong-reused-descriptor/hello",
                              true,&n,resolved)==-ENOENT);
    assert(nas_vfs_node_device(&first)!=nas_vfs_node_device(&second));
    assert(nas_vfs_link("/mnt/hello","/mnt-alias/linked")==-EXDEV);
    uint64_t original_ext_device=nas_vfs_node_device(&first);
    assert(nas_vfs_node_pin(&first)==0);
    assert(nas_vfs_unregister_mount(&alias_mount)==0);
    assert(nas_vfs_read_at(&second,0,(uint8_t *)data,1)==-ENODEV);
    /* Unrelated mount keeps identical stable superblock and device ID
     * after a hole opens in the registry (no array compaction). */
    assert(nas_vfs_node_device(&first)==original_ext_device);
    assert(nas_vfs_node_unpin(&first)==0);
    /* Registration must fail closed when a mountpoint is beneath a regular
     * file; neither prefix-based direct lookup nor VFS walk may bypass a
     * non-directory inode merely because a mountpoint string matches. */
    static const struct nas_vfs_mount under_file={
        .mountpoint="/bin/demo/mounted",.type=&ext_mock_type
    };
    assert(nas_vfs_register_mount(&under_file)==-ENOTDIR);
    struct nas_fs_node regular,found;
    assert(nas_fs_lookup_node("/bin/demo",true,&regular,resolved)==0);
    assert(nas_vfs_lookup_step(&regular,"/bin/demo/mounted","mounted",
                              &found)==-ENOTDIR);
    static const struct nas_vfs_mount nested_mount={
        .mountpoint="/mnt/nested",.type=&ext_mock_type,
        .allow_missing_mountpoint=true
    };
    assert(nas_vfs_register_mount(&nested_mount)==0);
    /* A new attachment reuses the freed alias slot, not its old device
     * incarnation. Borrowed handles from before reuse stay stale. */
    assert(nas_vfs_node_pin(&second)==-ESTALE);
    assert(nas_vfs_node_device(&second)==0);
    /* Parent mount owns a child reference and nested attachment pins the
     * parent root. Neither may be detached behind that attachment. */
    assert(nas_vfs_unregister_mount(&ext_mount)==-EBUSY);
    /* Direct longest-prefix lookup must NOT bypass the VFS dentry owner.
     * Simulate a provider returning a recycled parent-root identity while
     * the nested attachment still pins its original parent inode. */
    struct nas_vfs_path nested_parent={0},nested_source={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,"/mnt",true,
                                 &nested_parent)==0);
    assert(nas_fs_lookup_path_at(NULL,NULL,"/tmp",true,
                                 &nested_source)==0);
    assert(nas_vfs_rename_child(&nested_source,"demo",&nested_parent,
                                "nested",0)==-EBUSY);
    assert(nas_vfs_path_put(&nested_source)==0);
    assert(nas_vfs_path_put(&nested_parent)==0);
    ext_mock_root_ino=999;
    assert(nas_vfs_lookup("/mnt/nested/hello",&found)==-ENOENT);
    assert(nas_fs_lookup_node("/mnt/nested/hello",true,&found,resolved)==-ENOENT);
    ext_mock_root_ino=801;
    assert(nas_vfs_lookup("/mnt/nested/hello",&found)==0);
    assert(nas_fs_lookup_node("/mnt/nested/hello",true,&found,resolved)==0);
    assert(found.mount!=&nested_mount &&
           !strcmp(found.mount->mountpoint,"/mnt/nested") &&
           found.ino==first.ino);
    /* Across two independent mount transitions, Path binding also walks
     * each canonical edge once. Re-running namei for every prefix would
     * probe the bootfs root four times instead of once. */
    bootfs_find_calls=0;
    struct nas_vfs_path nested_path={0};
    assert(nas_vfs_path_bind(&found,"/mnt/nested/hello",&nested_path)==0);
    assert(bootfs_find_calls==1);
    assert(nas_vfs_path_put(&nested_path)==0);
    assert(nas_fs_lookup_node("/mnt/nested/../hello",true,&found,resolved)==0);
    assert(found.mount==first.mount && found.ino==first.ino);
    assert(nas_vfs_lookup("/mnt/nested/../hello",&found)==0);
    assert(found.mount==first.mount && found.ino==first.ino);
    assert(nas_fs_lookup_node("/mnt/hello/..",true,&found,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/mnt/nested/.",true,&found,resolved)==0);
    assert(!strcmp(found.mount->mountpoint,"/mnt/nested") && found.is_dir);
    assert(nas_vfs_unregister_mount(&nested_mount)==0);
    assert(nas_vfs_unregister_mount(&ext_mount)==0);
    assert(!nas_vfs_inode_is_pinned(&entries[5]));
    /* A direct ELF bootstrap with NO CPIO has no bootfs root. A separately
     * attached real mount must still resolve an executable by component
     * namei, without the missing archive creating a phantom '/' inode. */
    archive_available=false;
    assert(nas_fs_lookup_node("/",true,&n,resolved)==-ENOENT);
    static const struct nas_vfs_mount no_archive={
        .mountpoint="/standalone",.type=&ext_mock_type,
        .allow_missing_mountpoint=true
    };
    assert(nas_vfs_register_mount(&no_archive)==0);
    assert(nas_fs_lookup_node("/standalone/hello",true,&n,resolved)==0);
    assert(n.filesystem==&ext_mock_type && n.ino==802);
    assert(nas_vfs_lookup("/standalone/hello",&direct)==0);
    assert(direct.mount==n.mount && direct.ino==n.ino);
    /* The same open transaction must work before CPIO/root is available.
     * The deleted open_inode path used to handle this bootstrap case. */
    struct nas_fs_node rootless_open_node={0};
    struct nas_vfs_path rootless_open_path={0};
    struct nas_vfs_open_options rootless_open_options={
        .follow_final=true,.access=0
    };
    assert(nas_vfs_open_path_at(NULL,NULL,"/standalone/hello",
                                 &rootless_open_options,&rootless_open_node,
                                 &rootless_open_path)==0);
    assert(rootless_open_node.ino==802 && rootless_open_path.dentry);
    assert(nas_vfs_unregister_mount(&no_archive)==-EBUSY);
    assert(nas_vfs_node_unpin(&rootless_open_node)==0);
    assert(nas_vfs_path_put(&rootless_open_path)==0);
    assert(nas_vfs_open_path_at(NULL,NULL,"/standalone/missing",
                                 &rootless_open_options,&rootless_open_node,
                                 &rootless_open_path)==-ENOENT);
    assert(!rootless_open_path.dentry);
    assert(nas_vfs_lookup("/standalone/../no-root",&direct)==-ENOENT);
    struct nas_vfs_path rootless_mount_root={0},rootless_parent={0};
    assert(nas_fs_lookup_path_at(NULL,NULL,"/standalone",true,
                                 &rootless_mount_root)==0);
    assert(nas_vfs_path_parent(&rootless_mount_root,NULL,
                                &rootless_parent)==-ENOENT);
    assert(!rootless_parent.dentry);
    assert(nas_fs_lookup_path_at(NULL,NULL,"/standalone/..",true,
                                 &rootless_parent)==-ENOENT);
    assert(!rootless_parent.dentry);
    assert(nas_vfs_path_put(&rootless_mount_root)==0);
    struct nas_vfs_path rootless={0},rootless_copy={0};
    assert(nas_vfs_path_bind(&n,"/standalone/hello",&rootless)==0);
    assert(nas_vfs_path_get(&rootless,&rootless_copy)==0);
    char path_name[256];
    assert(nas_vfs_path_string(&rootless_copy,path_name)==0);
    assert(!strcmp(path_name,"/standalone/hello"));
    assert(nas_vfs_node_pin(&n)==0);
    assert(nas_vfs_unregister_mount(&no_archive)==-EBUSY);
    assert(nas_vfs_node_unpin(&n)==0);
    assert(nas_vfs_unregister_mount(&no_archive)==-EBUSY);
    assert(nas_vfs_path_put(&rootless_copy)==0);
    assert(nas_vfs_path_put(&rootless)==0);
    assert(nas_vfs_unregister_mount(&no_archive)==0);
    archive_available=true;
    assert(nas_fs_lookup_node("/standalone/hello",true,
                              &n,resolved)==-ENOENT);
    /* More than three full historical mount tables: the real shared VFS
     * must grow stable superblock addresses rather than impose 16 slots.
     * All names belong to the SAME independent provider; no per-provider
     * mount-special case is allowed to make this test succeed. */
    enum { STRESS_MOUNTS=80, REFILL_MOUNTS=32 };
    static struct nas_vfs_mount stress[STRESS_MOUNTS];
    static char stress_paths[STRESS_MOUNTS][40];
    /* Keep 80 independent mounted directory Paths alive simultaneously:
     * the positive Dentry lookup index must reuse the exact same object,
     * not allocate one C-heap object per repeated open/getcwd traversal. */
    static struct nas_vfs_path stress_paths_owned[STRESS_MOUNTS];
    struct nas_fs_node stable_early,stale_reusable,stress_parent,probe;
    struct nas_vfs_dirent stress_entries[7];
    uint64_t stable_device=0;
    for(unsigned i=0;i<STRESS_MOUNTS;i++) {
        int nbytes=snprintf(stress_paths[i],sizeof(stress_paths[i]),
                             "/tmp/stress-%02u",i);
        assert(nbytes>0 && (unsigned)nbytes<sizeof(stress_paths[i]));
        stress[i]=(struct nas_vfs_mount){
            .mountpoint=stress_paths[i],.type=&ext_mock_type,
            .allow_missing_mountpoint=true
        };
        assert(nas_vfs_register_mount(&stress[i])==0);
        if(i==0) {
            assert(nas_fs_lookup_node(stress_paths[i],true,
                                      &stable_early,resolved)==0);
            stable_device=nas_vfs_node_device(&stable_early);
            assert(stable_device!=0);
            assert(nas_vfs_node_pin(&stable_early)==0);
        }
        /* A pointer acquired from an early mount cannot move when the
         * 17th, 33rd or 49th slot causes chunk growth. */
        assert(nas_vfs_node_device(&stable_early)==stable_device);
        assert(nas_vfs_lookup(stress_paths[i],&probe)==0);
        assert(probe.mount!=&stress[i] && probe.ino==801);
        assert(nas_vfs_path_bind(&probe,stress_paths[i],
                                 &stress_paths_owned[i])==0);
        assert(stress_paths_owned[i].dentry);
        /* Repeated positive lookup and open through the same mount MUST
         * reuse its owned Dentry, including after slot-chunk expansion. */
        struct nas_vfs_path repeat={0};
        assert(nas_vfs_path_bind(&probe,stress_paths[i],&repeat)==0);
        assert(repeat.dentry==stress_paths_owned[i].dentry);
        assert(nas_vfs_path_put(&repeat)==0);
        if(i)assert(stress_paths_owned[i].dentry!=
                    stress_paths_owned[i-1].dentry);
    }
    /* Exercise hash collisions, full-bucket traversal and ancestor pins
     * from 80 live Paths without dropping/reacquiring backend mount roots.
     * Each copied Path is an independent owner of the SAME Dentry. */
    for(unsigned pass=0;pass<4;pass++)
        for(unsigned i=STRESS_MOUNTS;i-->0;) {
            struct nas_fs_node current;
            struct nas_vfs_path repeated={0},copied={0};
            assert(nas_fs_lookup_node(stress_paths[i],true,
                                      &current,resolved)==0);
            assert(nas_vfs_path_bind(&current,stress_paths[i],&repeated)==0);
            assert(repeated.dentry==stress_paths_owned[i].dentry);
            assert(nas_vfs_path_get(&repeated,&copied)==0);
            assert(copied.dentry==stress_paths_owned[i].dentry);
            assert(nas_vfs_path_string(&copied,path_name)==0);
            assert(!strcmp(path_name,stress_paths[i]));
            assert(nas_vfs_path_put(&repeated)==0);
            assert(nas_vfs_path_put(&copied)==0);
        }
    assert(nas_vfs_unregister_mount(&stress[0])==-EBUSY);
    assert(nas_vfs_unregister_mount(&stress[31])==-EBUSY);
    assert(nas_fs_lookup_node("/tmp",true,&stress_parent,resolved)==0);
    /* Virtual mountpoints must be presented exactly once and in stable
     * registry order, even when readdir is split into tiny batches. */
    for(unsigned i=0;i<STRESS_MOUNTS;i++) {
        assert(nas_vfs_read_dir(&stress_parent,2+i,
                                stress_entries,1)==1);
        assert(!strcmp(stress_entries[0].name,stress_paths[i]+5));
        assert(stress_entries[0].ino==801);
        assert(stress_entries[0].dtype==4);
    }
    assert(nas_vfs_read_dir(&stress_parent,0,stress_entries,7)==7);
    assert(!strcmp(stress_entries[0].name,"."));
    assert(!strcmp(stress_entries[1].name,".."));
    assert(!strcmp(stress_entries[2].name,stress_paths[0]+5));
    /* Nested mount is owned by the actual parent mount, even when both
     * attachments live in dynamically allocated storage chunks. */
    static const struct nas_vfs_mount stress_nested={
        .mountpoint="/tmp/stress-47/nested",.type=&ext_mock_type,
        .allow_missing_mountpoint=true
    };
    assert(nas_vfs_register_mount(&stress_nested)==0);
    assert(nas_vfs_unregister_mount(&stress[47])==-EBUSY);
    assert(nas_fs_lookup_node("/tmp/stress-47/nested/hello",true,
                              &probe,resolved)==0);
    assert(probe.ino==802);
    assert(nas_vfs_unregister_mount(&stress_nested)==0);
    /* Free holes spanning static and heap chunks, then reuse them.
     * A stale inode cannot inherit a different mount incarnation when
     * the SAME slot address and backend inode number are recycled. */
    assert(nas_fs_lookup_node(stress_paths[1],true,
                              &stale_reusable,resolved)==0);
    /* A Path-owner remains live after all temporary copies close.
     * Releasing the entire graph must also release ancestors without
     * recursive kernel C-stack teardown or a dangling hash entry. */
    for(unsigned i=STRESS_MOUNTS;i-->0;) {
        assert(nas_vfs_path_string(&stress_paths_owned[i],path_name)==0);
        assert(!strcmp(path_name,stress_paths[i]));
        assert(nas_vfs_path_put(&stress_paths_owned[i])==0);
        assert(!stress_paths_owned[i].dentry);
    }
    for(unsigned i=1;i<STRESS_MOUNTS;i+=2)
        assert(nas_vfs_unregister_mount(&stress[i])==0);
    static struct nas_vfs_mount refill[REFILL_MOUNTS];
    static char refill_paths[REFILL_MOUNTS][40];
    for(unsigned i=0;i<REFILL_MOUNTS;i++) {
        int nbytes=snprintf(refill_paths[i],sizeof(refill_paths[i]),
                            "/tmp/refill-%02u",i);
        assert(nbytes>0 && (unsigned)nbytes<sizeof(refill_paths[i]));
        refill[i]=(struct nas_vfs_mount){
            .mountpoint=refill_paths[i],.type=&ext_mock_type,
            .allow_missing_mountpoint=true
        };
        assert(nas_vfs_register_mount(&refill[i])==0);
    }
    assert(nas_vfs_node_pin(&stale_reusable)==-ESTALE);
    assert(nas_vfs_node_device(&stale_reusable)==0);
    assert(nas_vfs_node_device(&stable_early)==stable_device);
    assert(nas_fs_lookup_node(refill_paths[0],true,&probe,resolved)==0);
    /* Recycled slots intentionally retain the same superblock ADDRESS.
     * Only the new mount_id prevents use of the old borrowed inode. */
    assert(probe.mount==stale_reusable.mount &&
           probe.mount_id!=stale_reusable.mount_id && probe.ino==801);
    assert(nas_vfs_node_unpin(&stable_early)==0);
    for(unsigned i=0;i<STRESS_MOUNTS;i+=2)
        assert(nas_vfs_unregister_mount(&stress[i])==0);
    for(unsigned i=0;i<REFILL_MOUNTS;i++)
        assert(nas_vfs_unregister_mount(&refill[i])==0);
    assert(nas_fs_lookup_node(stress_paths[0],true,&probe,resolved)==-ENOENT);
    assert(nas_fs_lookup_node(refill_paths[0],true,&probe,resolved)==-ENOENT);
    puts("[VFS PROVIDER PASS] real mount dispatch, pathwalk, CPIO/tmp/proc, symlinks, kernel read, inode ops");
    return 0;
}
