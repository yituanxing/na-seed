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
static struct nas_tmp_file fixture_symlink={.used=true,.mode=0120777,.size=9,.ino=102};
static struct nas_tmp_file fixture_dir_link={.used=true,.mode=0120777,.size=4,.ino=103};
static struct nas_tmp_file fixture_created={.used=true,.mode=0100640,.ino=104};
/* A named directory is renamed while its original inode remains open. */
static struct nas_tmp_file fixture_open_directory={
    .used=true,.is_dir=true,.mode=0040755,.nlink=1,.ino=105
};
static struct nas_tmp_file fixture_open_child={
    .used=true,.mode=0100600,.nlink=1,.ino=106
};
static struct nas_tmp_file fixture_orphan={
    .used=true,.mode=0100600,.nlink=0,.ino=107
};
static unsigned orphan_releases;
void tmp_inode_discard(struct nas_tmp_file *node) {
    /* An actual kernel reclaims only orphaned, unpinned inodes; the
     * host fixture records the final provider callback. */
    if(node==&fixture_orphan && !nas_vfs_inode_is_pinned(node)) {
        node->used=false;
        orphan_releases++;
    }
}
static char fixture_dir_path[256]="/tmp/opened";
static char fixture_child_path[256]="/tmp/opened/child";
static uint8_t fixture_created_bytes[32];
static unsigned fixture_created_length;
bool bootfs_mounted(void){return true;}
const struct bootfs_entry *bootfs_find(const char *path){
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
    if(!strcmp(path,"/tmp/demo"))return &tmp_demo;
    if(!strcmp(path,"/tmp/link"))return &fixture_symlink;
    if(!strcmp(path,"/tmp/alias"))return &fixture_dir_link;
    if(!strcmp(path,"/tmp/newfile") && fixture_created_length)return &fixture_created;
    if(!strcmp(path,fixture_dir_path))return &fixture_open_directory;
    if(!strcmp(path,fixture_child_path))return &fixture_open_child;
    if(!strcmp(path,"/tmp/pinned-orphan"))return &fixture_orphan;
    return NULL;
}
const char *tmp_name(const struct nas_tmp_file *f) {
    if(f==&fixture_open_directory)return fixture_dir_path;
    return NULL;
}
bool tmp_is_link(const struct nas_tmp_file *f){
    return f && (f->mode&0170000U)==0120000U;
}
int tmp_link_target(const struct nas_tmp_file *f,char target[256]){
    if(f==&fixture_dir_link){memcpy(target,"/tmp",5);return 0;}
    if(f!=&fixture_symlink)return -EINVAL;
    memcpy(target,abs_link,sizeof(abs_link));return 0;
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
    .used=true,.path="/tmp/demo",.inode=&tmp_demo
};
static const struct nas_tmp_dentry link_dentry={
    .used=true,.path="/tmp/link",.inode=&fixture_symlink
};
static struct nas_tmp_dentry nested_dentry={
    .used=true,.path="/tmp/opened/child",.inode=&fixture_open_child
};
const struct nas_tmp_dentry *tmp_child_entry(const char *path,unsigned index) {
    if(!strcmp(path,fixture_dir_path)) {
        strcpy(nested_dentry.path,fixture_child_path);
        return index==0?&nested_dentry:NULL;
    }
    if(strcmp(path,"/tmp"))return NULL;
    return index==0?&demo_dentry:index==1?&link_dentry:NULL;
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
static int ext_mock_lookup(const struct nas_vfs_mount *mount,
                           const char *path,struct nas_fs_node *node){
    (void)mount;
    mounted_lookups++;
    if(strcmp(path,"/mnt/hello") && strcmp(path,"/mnt"))return -ENOENT;
    *node=!strcmp(path,"/mnt")
        ? (struct nas_fs_node){.ino=801,.mode=0040755U,.is_dir=true}
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
int main(void) {
    char resolved[256],data[32];
    struct nas_fs_node n;
    struct nas_fs_node canonical_parent;
    assert(nas_fs_lookup_parent("/tmp/alias/new-parent",
                  &canonical_parent,resolved)==0);
    assert(!strcmp(resolved,"/tmp/new-parent") && canonical_parent.is_dir);
    assert(nas_fs_canonical_candidate("/tmp/alias/new-parent",resolved)==0);
    assert(!strcmp(resolved,"/tmp/new-parent"));
    assert(nas_fs_canonical_candidate("/bin/demo/../new",resolved)==-ENOTDIR);
    assert(nas_fs_lookup_parent("/no-intermediate/new",
                  &canonical_parent,resolved)==-ENOENT);
    assert(nas_vfs_join_child("/tmp","good",resolved)==0 &&
           !strcmp(resolved,"/tmp/good"));
    assert(nas_vfs_join_child("/tmp","../bad",resolved)==-EINVAL);
    assert(nas_vfs_join_child("/tmp",".",resolved)==-EINVAL);
    assert(nas_vfs_join_child("/tmp","..",resolved)==-EINVAL);
    assert(nas_fs_lookup_node("/bin/demo",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo") && !strcmp(n.filesystem->name,"bootfs"));
    assert(nas_vfs_read_at(&n,0,(uint8_t *)data,4)==4);
    /* Open policy belongs to one VFS inode transaction, not one
     * Linux syscall branch per provider. The returned node is pinned. */
    struct nas_fs_node opened;
    struct nas_vfs_open_options open_options={.follow_final=true,.access=0};
    assert(nas_vfs_open_inode("/bin/demo",&open_options,
                              &opened,resolved)==0);
    assert(opened.mount && opened.ino==n.ino);
    assert(nas_vfs_node_unpin(&opened)==0);
    open_options.access=1;
    assert(nas_vfs_open_inode("/bin/demo",&open_options,
                              &opened,resolved)==-EROFS);
    open_options.access=0;
    open_options.directory_only=true;
    assert(nas_vfs_open_inode("/bin/demo",&open_options,
                              &opened,resolved)==-ENOTDIR);
    open_options.create=true;open_options.exclusive=true;
    open_options.directory_only=false;
    assert(nas_vfs_open_inode("/bin/demo",&open_options,
                              &opened,resolved)==-EEXIST);
    open_options.directory_only=true;
    assert(nas_vfs_open_inode("/tmp/open-never-created",&open_options,
                              &opened,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/tmp/open-never-created",true,
                              &opened,resolved)==-ENOENT);
    assert(!memcmp(data,elf_bytes,4) && !nas_vfs_can_write(&n));
    assert(nas_vfs_chmod(&n,0666)==-EROFS);
    assert(nas_fs_lookup_node("/tmp/demo",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"tmpfs") && nas_vfs_can_write(&n));
    assert(nas_vfs_read_at(&n,1,(uint8_t *)data,5)==3);
    assert(!memcmp(data,"ATA",3));
    assert(nas_vfs_chmod(&n,0600)==0 && tmp_demo.mode==0100600U);
    assert(nas_vfs_utimens(&n,123,456,true,true)==0);
    assert(tmp_demo.atime_ns==123 && tmp_demo.mtime_ns==456);
    struct nas_vfs_dirent records[8];
    assert(nas_fs_lookup_node("/",true,&n,resolved)==0);
    assert(nas_vfs_read_dir(&n,0,records,8)==7);
    assert(!strcmp(records[0].name,".") && records[0].ino==1);
    assert(!strcmp(records[1].name,"..") && records[1].ino==1);
    assert(!strcmp(records[2].name,"bin") && records[2].ino==2);
    assert(records[2].dtype==4);
    assert(nas_vfs_read_dir(&n,7,records,8)==0);
    assert(nas_fs_lookup_node("/bin",true,&n,resolved)==0);
    assert(nas_vfs_read_dir(&n,0,records,8)==3);
    assert(records[2].ino==3 && !strcmp(records[2].name,"demo"));
    assert(nas_fs_lookup_node("/tmp",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"tmpfs"));
    assert(nas_vfs_read_dir(&n,0,records,8)==4);
    assert(!strcmp(records[2].name,"demo") && records[2].ino==5101);
    assert(!strcmp(records[3].name,"link") && records[3].dtype==10);
    assert(nas_vfs_read_dir(&n,2,records,1)==1);
    assert(!strcmp(records[0].name,"demo"));
    /* Reopening an old pathname after rename is forbidden: VFS directory
     * enumerations must follow the stable pinned inode identity instead. */
    struct nas_fs_node opened_directory;
    assert(nas_fs_lookup_node("/tmp/opened",true,
                              &opened_directory,resolved)==0);
    assert(nas_vfs_node_pin(&opened_directory)==0);
    assert(nas_vfs_rename("/tmp/opened","/tmp/renamed-opened",0)==0);
    assert(nas_fs_lookup_node("/tmp/opened",true,&n,resolved)==-ENOENT);
    assert(nas_vfs_read_dir(&opened_directory,
                            0,records,8)==3);
    assert(!strcmp(records[2].name,"child") && records[2].ino==5106);
    char path_from_inode[256];
    assert(nas_vfs_directory_path(&opened_directory,"/tmp/opened",
                                  path_from_inode)==0);
    assert(!strcmp(path_from_inode,"/tmp/renamed-opened"));
    struct nas_fs_node read_only_dir;
    assert(nas_fs_lookup_node("/bin",true,&read_only_dir,resolved)==0);
    assert(nas_vfs_directory_path(&read_only_dir,"/invalid",path_from_inode)==0);
    assert(!strcmp(path_from_inode,"/bin"));
    assert(nas_vfs_node_unpin(&opened_directory)==0);
    /* Even with no live lease, a recycled (mount,ino) must not make a
     * different backend inode valid for dirfd/cwd-relative pathwalk. */
    struct nas_fs_node recycled_directory=opened_directory;
    recycled_directory.inode=&fixture_open_child;
    assert(nas_vfs_directory_path(&recycled_directory,
                "/tmp/renamed-opened",path_from_inode)==-ESTALE);
    assert(nas_fs_lookup_node("/proc",true,&n,resolved)==0);
    assert(nas_vfs_read_dir(&n,0,records,8)==3);
    assert(!strcmp(records[2].name,"self") && records[2].dtype==4);
    assert(nas_vfs_read_dir(&n,3,records,8)==0);
    assert(nas_fs_lookup_node("/tmp/archive",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"bootfs")); /* legacy mount fallthrough */
    assert(nas_fs_lookup_node("/proc/self",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"procfs") && n.is_dir);
    assert(nas_vfs_directory_path(&n,"/proc/self",path_from_inode)==0);
    assert(!strcmp(path_from_inode,"/proc"));
    assert(nas_fs_lookup_node("/proc/self/fd/3",false,&n,resolved)==0);
    assert((n.mode&0170000U)==0120000U && !n.is_symlink);
    char fd_target[256]={0};
    assert(nas_vfs_readlink(&n,fd_target)==9);
    assert(!memcmp(fd_target,"pipe:[42]",9));
    assert(nas_vfs_create("/tmp/newfile",0640)==0);
    assert(nas_vfs_create("/tmp/newfile",0640)==-EEXIST);
    assert(nas_fs_lookup_node("/tmp/newfile",true,&n,resolved)==0);
    assert(nas_vfs_write_at(&n,0,(const uint8_t *)"xyz",3)==3);
    assert(nas_fs_lookup_node("/tmp/newfile",true,&n,resolved)==0);
    assert(nas_vfs_read_at(&n,0,(uint8_t *)data,3)==3 && !memcmp(data,"xyz",3));
    /* Saturate the VFS's owned-inode table with distinct fixture identities
     * before O_TRUNC and O_CREAT. Neither operation may mutate namespace or
     * data when a new open File cannot acquire its mandatory inode lease. */
    struct nas_fs_node capacity_pins[257];
    unsigned capacity_used=0;
    for(;capacity_used<257;capacity_used++) {
        capacity_pins[capacity_used]=n;
        capacity_pins[capacity_used].ino=100000U+capacity_used;
        int rc=nas_vfs_node_pin(&capacity_pins[capacity_used]);
        if(rc==-ENFILE)break;
        assert(rc==0);
    }
    assert(capacity_used>0 && capacity_used<=256);
    struct nas_fs_node failed_open;
    char failed_path[256];
    struct nas_vfs_open_options fail_options={
        .follow_final=true,.access=2,.truncate=true
    };
    assert(nas_vfs_open_inode("/tmp/newfile",&fail_options,
                              &failed_open,failed_path)==-ENFILE);
    fail_options.truncate=false;
    fail_options.create=true;
    fail_options.create_mode=0640;
    assert(nas_vfs_open_inode("/tmp/open-never-created",&fail_options,
                              &failed_open,failed_path)==-ENFILE);
    assert(nas_fs_lookup_node("/tmp/open-never-created",true,
                               &failed_open,failed_path)==-ENOENT);
    assert(fixture_created_length==3 && fixture_created.size==3);
    assert(nas_vfs_read_at(&n,0,(uint8_t *)data,3)==3 &&
           !memcmp(data,"xyz",3));
    for(unsigned i=0;i<capacity_used;i++)
        assert(nas_vfs_node_unpin(&capacity_pins[i])==0);
    assert(nas_vfs_node_pin(&n)==0);
    /* Linux locks are VFS inode + owned File services. Independent
     * opens contend even when backend directory names are recycled. */
    static const unsigned owner_a=7,owner_b=8;
    assert(nas_vfs_flock(&n,&owner_a,2)==0);
    assert(nas_vfs_flock(&n,&owner_b,1)==-EWOULDBLOCK);
    assert(nas_vfs_flock(&n,&owner_a,8)==0);
    assert(nas_vfs_flock(&n,&owner_b,1)==0);
    assert(nas_vfs_flock(&n,&owner_a,2)==-EWOULDBLOCK);
    nas_vfs_release_flock(&owner_b);
    assert(nas_vfs_flock(&n,&owner_a,2)==0);
    struct nas_posix_locks *locks=nas_vfs_posix_locks(&n);
    assert(locks);
    assert(nas_locks_apply(locks,11,NAS_LOCK_WRITE,0,4)==0);
    assert(nas_locks_conflict(locks,12,NAS_LOCK_WRITE,0,4)>=0);
    nas_vfs_release_posix_locks(&n,11);
    assert(nas_locks_conflict(locks,12,NAS_LOCK_WRITE,0,4)==-1);
    struct nas_fs_node other_open=n;
    assert(nas_vfs_node_pin(&other_open)==0);
    assert(nas_vfs_resize(&n,2)==0 && fixture_created.size==2);
    assert(nas_vfs_node_size(&other_open)==2);
    assert(nas_vfs_node_unpin(&other_open)==0);
    assert(nas_vfs_node_unpin(&n)==0);
    /* All flock entries on this inode disappear on last unpin, so an
     * allocator cannot inherit stale process or File ownership. */
    assert(nas_vfs_node_pin(&n)==0);
    assert(nas_vfs_flock(&n,&owner_b,1)==0);
    nas_vfs_release_flock(&owner_b);
    assert(nas_vfs_node_unpin(&n)==0);
    assert(nas_vfs_sync(&n)==0);
    struct nas_fs_node orphan;
    assert(nas_fs_lookup_node("/tmp/pinned-orphan",true,&orphan,resolved)==0);
    assert(orphan.inode==&fixture_orphan);
    assert(nas_vfs_node_pin(&orphan)==0);
    assert(nas_vfs_inode_is_pinned(&fixture_orphan));
    /* Open File keeps its inode even after unlink. Refresh st_nlink=0. */
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
    assert(nas_fs_lookup_node("/shortcut",false,&n,resolved)==0 && n.is_symlink);
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
    assert(nas_vfs_register_mount(&ext_mount)==0);
    /* The mount itself owns the covered /mnt directory and root parent.
     * A directory lease cannot evaporate merely because no user FD is open. */
    assert(nas_vfs_inode_is_pinned(&entries[5]));
    assert(nas_vfs_mkdir("/mnt",0755)==-EEXIST);
    assert(nas_vfs_unlink("/mnt",0x200ULL)==-EBUSY);
    assert(nas_vfs_unlink("/mnt",0x800ULL)==-EINVAL);
    assert(nas_vfs_rename("/mnt","/tmp/renamed",0)==-EBUSY);
    assert(nas_vfs_rename("/tmp/demo","/mnt",0)==-EBUSY);
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
    static const struct nas_vfs_mount alias_mount={
        .mountpoint="/mnt-alias",.type=&ext_mock_type
    };
    assert(nas_vfs_register_mount(&alias_mount)==0);
    struct nas_fs_node first,second;
    assert(nas_vfs_lookup("/mnt/hello",&first)==0);
    assert(nas_vfs_lookup("/mnt-alias/hello",&second)==0);
    assert(first.ino==second.ino && first.mount!=second.mount);
    /* The real, shared namei pathwalk must obey exactly the same
     * mount-root/child contract as the direct kernel VFS lookup. */
    assert(nas_fs_lookup_node("/mnt-alias/hello",true,&second,resolved)==0);
    assert(second.mount==&alias_mount && second.ino==first.ino);
    assert(nas_vfs_node_device(&first)!=nas_vfs_node_device(&second));
    assert(nas_vfs_link("/mnt/hello","/mnt-alias/linked")==-EXDEV);
    assert(nas_vfs_unregister_mount(&alias_mount)==0);
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
        .mountpoint="/mnt/nested",.type=&ext_mock_type
    };
    assert(nas_vfs_register_mount(&nested_mount)==0);
    /* Parent mount owns a child reference and nested attachment pins the
     * parent root. Neither may be detached behind that attachment. */
    assert(nas_vfs_unregister_mount(&ext_mount)==-EBUSY);
    assert(nas_fs_lookup_node("/mnt/nested/hello",true,&found,resolved)==0);
    assert(found.mount==&nested_mount && found.ino==first.ino);
    assert(nas_fs_lookup_node("/mnt/nested/../hello",true,&found,resolved)==0);
    assert(found.mount==&ext_mount && found.ino==first.ino);
    assert(nas_fs_lookup_node("/mnt/hello/..",true,&found,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/mnt/nested/.",true,&found,resolved)==0);
    assert(found.mount==&nested_mount && found.is_dir);
    assert(nas_vfs_unregister_mount(&nested_mount)==0);
    assert(nas_vfs_unregister_mount(&ext_mount)==0);
    assert(!nas_vfs_inode_is_pinned(&entries[5]));
    puts("[VFS PROVIDER PASS] real mount dispatch, pathwalk, CPIO/tmp/proc, symlinks, kernel read, inode ops");
    return 0;
}
