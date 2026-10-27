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
    {.path="/",.mode=0040755},
    {.path="/bin",.mode=0040755},
    {.path="/bin/demo",.mode=0100755,.data=elf_bytes,.size=sizeof(elf_bytes)},
    {.path="/tmp",.mode=0041777},
    {.path="/proc",.mode=0040555},
    {.path="/mnt",.mode=0040755},
    {.path="/tmp/archive",.mode=0100444,.data=elf_bytes,.size=sizeof(elf_bytes)},
    {.path="/shortcut",.mode=0120777,.data=abs_link,.size=sizeof(abs_link)-1}
};
static struct nas_tmp_file tmp_demo={.used=true,.mode=0100644,.size=4};
static struct nas_tmp_file fixture_symlink={.used=true,.mode=0120777,.size=9};
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
    return NULL;
}
bool tmp_is_link(const struct nas_tmp_file *f){
    return f && (f->mode&0170000U)==0120000U;
}
int tmp_link_target(const struct nas_tmp_file *f,char target[256]){
    if(f!=&fixture_symlink)return -EINVAL;
    memcpy(target,abs_link,sizeof(abs_link));return 0;
}
int64_t tmp_read_at(const struct nas_tmp_file *file,uint64_t off,uint8_t *buf,uint64_t len){
    static const char value[]="DATA";
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
    if(strcmp(path,"/proc") && strcmp(path,"/proc/self"))return false;
    *out=(struct nas_proc_node){.kind=NAS_PROC_ROOT};return true;
}
uint32_t nas_proc_mode(struct nas_proc_node node){(void)node;return 0040555U;}
bool nas_proc_is_dir(struct nas_proc_node node){(void)node;return true;}
static unsigned mounted_lookups;
static int ext_mock_lookup(const char *path,struct nas_fs_node *node){
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
    assert(nas_fs_lookup_node("/tmp/archive",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"bootfs")); /* legacy mount fallthrough */
    assert(nas_fs_lookup_node("/proc/self",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"procfs") && n.is_dir);
    assert(nas_fs_lookup_node("/shortcut",false,&n,resolved)==0 && n.is_symlink);
    assert(nas_fs_lookup_node("/shortcut",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo"));
    assert(nas_fs_lookup_node("/tmp/link",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo"));
    assert(nas_fs_lookup_node("/tmp/../bin/demo",true,&n,resolved)==0);
    assert(!strcmp(resolved,"/bin/demo"));
    assert(nas_fs_lookup_node("/bin/demo/no",true,&n,resolved)==-ENOTDIR);
    assert(nas_fs_lookup_node("/missing",true,&n,resolved)==-ENOENT);
    assert(nas_vfs_register_mount(&ext_mount)==0);
    assert(nas_vfs_register_mount(&ext_mount)==-EEXIST);
    assert(nas_fs_lookup_node("/mnt/hello",true,&n,resolved)==0);
    assert(!strcmp(n.filesystem->name,"ext-mount-mock"));
    assert(mounted_lookups!=0);
    assert(nas_fs_lookup_node("/mnt/no",true,&n,resolved)==-ENOENT);
    assert(nas_fs_lookup_node("/mnt2",true,&n,resolved)==-ENOENT);
    puts("[VFS PROVIDER PASS] real mount dispatch, pathwalk, CPIO/tmp/proc, symlinks, kernel read, inode ops");
    return 0;
}
