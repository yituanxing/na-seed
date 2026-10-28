/* Original upstream LittleFS mounted via the genuine NaSeed VFS and namei. */
#include "../ports/littlefs/vfs_port.h"
#include "../kernel/block/ramdisk.h"
#include "../kernel/fs/lookup.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int root_lookup(const struct nas_vfs_mount *m,const char *path,
                       struct nas_fs_node *node){
    (void)m;
    if(strcmp(path,"/") && strcmp(path,"/flash"))return -ENOENT;
    *node=(struct nas_fs_node){.ino=1,.nlink=2,.is_dir=true,.mode=0040755};
    return 0;
}
static int root_get(const struct nas_vfs_mount *m,struct nas_fs_node *out) {
    return root_lookup(m,"/",out);
}
static int root_child(const struct nas_fs_node *parent,const char *name,
                      struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    return !strcmp(name,"flash") ?
        root_lookup(parent->mount,"/flash",out) : -ENOENT;
}
static const struct nas_vfs_type root_type={
    .name="fixture-root",.root=root_get,.lookup_child=root_child,
    .read_only=true
};
static const struct nas_vfs_mount root={
    .mountpoint="/",.type=&root_type,.permanent=true
};
void nas_vfs_register_default_mounts(void){
    assert(nas_vfs_register_mount(&root)==0);
}
int main(void) {
    size_t bytes=2U*1024U*1024U;
    uint8_t *image=malloc(bytes);assert(image);
    memset(image,0xff,bytes);
    struct nas_ramdisk disk={0};
    assert(nas_ramdisk_attach(&disk,"lfs-disk",image,bytes,512,true)==0);
    struct nas_littlefs_block format={0};
    assert(nas_littlefs_block_attach(&format,"lfs-disk")==0);
    lfs_t upstream={0};
    assert(lfs_format(&upstream,&format.config)==0);
    assert(lfs_mount(&upstream,&format.config)==0);
    assert(lfs_mkdir(&upstream,"/source")==0);
    lfs_file_t f={0};
    static const char payload[]="Second independent real filesystem through NaSeed VFS";
    assert(lfs_file_open(&upstream,&f,"/source/program",
                         LFS_O_CREAT|LFS_O_WRONLY)==0);
    assert(lfs_file_write(&upstream,&f,payload,sizeof(payload)-1)==
           (lfs_ssize_t)sizeof(payload)-1);
    assert(lfs_file_close(&upstream,&f)==0);
    assert(lfs_unmount(&upstream)==0);
    nas_littlefs_block_detach(&format);
    static struct nas_littlefs_vfs vfs;
    assert(nas_littlefs_vfs_mount(&vfs,"lfs-disk","/flash")==0);
    assert(nas_ramdisk_detach(&disk)==-EBUSY);
    struct nas_fs_node node,again,dir;char path[256];
    assert(nas_fs_lookup_node("/flash/source/program",true,&node,path)==0);
    assert(!node.is_dir && node.ino && node.size==sizeof(payload)-1);
    assert(!strcmp(node.filesystem->name,"littlefs"));
    uint8_t readback[sizeof(payload)]={0};
    assert(nas_vfs_read_at(&node,0,readback,sizeof(readback))==
           (int64_t)sizeof(payload)-1);
    assert(!memcmp(payload,readback,sizeof(payload)-1));
    assert(nas_fs_lookup_node("/flash/source/program",true,&again,path)==0);
    assert(again.ino==node.ino);
    assert(nas_fs_lookup_node("/flash/source",true,&dir,path)==0 && dir.is_dir);
    char live_directory[256];
    assert(nas_vfs_directory_path(&dir,"/flash/source",live_directory)==0);
    assert(!strcmp(live_directory,"/flash/source"));
    assert(nas_vfs_directory_path(&dir,"/flash/absent",live_directory)==-ESTALE);
    struct nas_vfs_dirent entries[8]={0};
    int n=nas_vfs_read_dir(&dir,0,entries,8);
    assert(n>=3);
    bool found=false;
    for(int i=0;i<n;i++)if(!strcmp(entries[i].name,"program")){
        assert(entries[i].ino==node.ino && entries[i].dtype==8);
        found=true;
    }
    assert(found);
    assert(nas_vfs_read_dir(&dir,n,entries,8)==0);
    assert(nas_vfs_create("/flash/source/new",0644)==-EROFS);
    assert(nas_vfs_unlink("/flash/source/program",0)==-EROFS);
    assert(nas_vfs_rename("/flash/source/program",
                          "/flash/source/other",0)==-EROFS);
    assert(nas_vfs_node_pin(&node)==0);
    assert(nas_littlefs_vfs_unmount(&vfs)==-EBUSY);
    assert(nas_vfs_node_unpin(&node)==0);
    assert(nas_littlefs_vfs_unmount(&vfs)==0);
    assert(nas_fs_lookup_node("/flash/source/program",true,
                              &again,path)==-ENOENT);
    assert(nas_littlefs_vfs_mount(&vfs,"lfs-disk","/flash")==0);
    assert(nas_fs_lookup_node("/flash/source/program",true,&again,path)==0);
    assert(nas_vfs_read_at(&again,0,readback,sizeof(readback))==
           (int64_t)sizeof(payload)-1);
    assert(!memcmp(payload,readback,sizeof(payload)-1));
    assert(nas_littlefs_vfs_unmount(&vfs)==0);
    assert(nas_ramdisk_detach(&disk)==0);
    free(image);
    puts("[UPSTREAM LITTLEFS VFS PASS] actual VFS namei, inode IDs, RO, readdir, mount pins, persistence");
    return 0;
}
