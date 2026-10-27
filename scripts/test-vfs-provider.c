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
static const struct nas_tmp_dentry demo_dentry={
    .used=true,.path="/tmp/demo",.inode=&tmp_demo
};
static const struct nas_tmp_dentry link_dentry={
    .used=true,.path="/tmp/link",.inode=&fixture_symlink
};
const struct nas_tmp_dentry *tmp_child_entry(const char *path,unsigned index) {
    if(strcmp(path,"/tmp"))return NULL;
    return index==0?&demo_dentry:index==1?&link_dentry:NULL;
}
uint64_t nas_proc_ino(struct nas_proc_node p) {(void)p;return 7000;}
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
        ? (struct nas_fs_node){.mode=0040755U,.is_dir=true}
        : (struct nas_fs_node){.mode=0100644U,.size=4};
    return 0;
}
static const struct nas_vfs_type ext_mock_type={
    .name="ext-mount-mock",.lookup=ext_mock_lookup,.read_only=true
};
static const struct nas_vfs_mount ext_mount={
    .mountpoint="/mnt",.type=&ext_mock_type
};
int main(void) {
    char resolved[256],data[32];
    struct nas_fs_node n;
    assert(nas_fs_lookup_node("/bin/demo",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo") && !strcmp(n.filesystem->name,"bootfs"));
    assert(nas_vfs_read_at(&n,0,(uint8_t *)data,4)==4);
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
    assert(nas_vfs_read_dir(&n,"/",0,records,8)==7);
    assert(!strcmp(records[0].name,".") && records[0].ino==1);
    assert(!strcmp(records[1].name,"..") && records[1].ino==1);
    assert(!strcmp(records[2].name,"bin") && records[2].ino==2);
    assert(records[2].dtype==4);
    assert(nas_vfs_read_dir(&n,"/",7,records,8)==0);
    assert(nas_fs_lookup_node("/bin",true,&n,resolved)==0);
    assert(nas_vfs_read_dir(&n,"/bin",0,records,8)==3);
    assert(records[2].ino==3 && !strcmp(records[2].name,"demo"));
    assert(nas_fs_lookup_node("/tmp",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"tmpfs"));
    assert(nas_vfs_read_dir(&n,"/tmp",0,records,8)==4);
    assert(!strcmp(records[2].name,"demo") && records[2].ino==5101);
    assert(!strcmp(records[3].name,"link") && records[3].dtype==10);
    assert(nas_vfs_read_dir(&n,"/tmp",2,records,1)==1);
    assert(!strcmp(records[0].name,"demo"));
    assert(nas_fs_lookup_node("/proc",true,&n,resolved)==0);
    assert(nas_vfs_read_dir(&n,"/proc",0,records,8)==3);
    assert(!strcmp(records[2].name,"self") && records[2].dtype==4);
    assert(nas_vfs_read_dir(&n,"/proc",3,records,8)==0);
    assert(nas_fs_lookup_node("/tmp/archive",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"bootfs")); /* legacy mount fallthrough */
    assert(nas_fs_lookup_node("/proc/self",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"procfs") && n.is_dir);
    assert(nas_fs_lookup_node("/proc/self/fd/3",false,&n,resolved)==0);
    assert((n.mode&0170000U)==0120000U && !n.is_symlink);
    char fd_target[256]={0};
    assert(nas_vfs_readlink(&n,resolved,fd_target)==9);
    assert(!memcmp(fd_target,"pipe:[42]",9));
    assert(nas_vfs_create("/tmp/newfile",0640)==0);
    assert(nas_vfs_create("/tmp/newfile",0640)==-EEXIST);
    assert(nas_fs_lookup_node("/tmp/newfile",true,&n,resolved)==0);
    assert(nas_vfs_write_at(&n,0,(const uint8_t *)"xyz",3)==3);
    assert(nas_fs_lookup_node("/tmp/newfile",true,&n,resolved)==0);
    assert(nas_vfs_read_at(&n,0,(uint8_t *)data,3)==3 && !memcmp(data,"xyz",3));
    assert(nas_vfs_node_pin(&n)==0);
    struct nas_fs_node other_open=n;
    assert(nas_vfs_node_pin(&other_open)==0);
    assert(nas_vfs_resize(&n,2)==0 && fixture_created.size==2);
    assert(nas_vfs_node_size(&other_open)==2);
    assert(nas_vfs_node_unpin(&other_open)==0);
    assert(nas_vfs_node_unpin(&n)==0);
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
    assert(nas_vfs_register_mount(&ext_mount)==0);
    assert(nas_vfs_mkdir("/mnt",0755)==-EEXIST);
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
    assert(nas_vfs_unregister_mount(&ext_mount)==0);
    assert(nas_fs_lookup_node("/mnt/hello",true,&n,resolved)==-ENOENT);
    assert(nas_vfs_unregister_mount(&ext_mount)==-ENOENT);
    assert(nas_vfs_register_mount(&ext_mount)==0);
    puts("[VFS PROVIDER PASS] real mount dispatch, pathwalk, CPIO/tmp/proc, symlinks, kernel read, inode ops");
    return 0;
}
