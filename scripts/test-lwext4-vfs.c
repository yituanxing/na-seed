/* The real VFS namei -> lwext4 inode -> BlockDevice/RamDisk integration
 * against the host-created authentic ext4 image; not a guest syscall test. */
#include "../kernel/fs/lookup.h"
#include "../kernel/fs/vfs.h"
#include "../ports/lwext4/guest_vfs.h"
#include "../kernel/errno.h"
#include "../kernel/task/resources.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void nas_print(const char *s) { fputs(s,stderr); }
void nas_arch_halt(void) { abort(); }
static int root_lookup(const struct nas_vfs_mount *mount,const char *path,
                       struct nas_fs_node *node) {
    (void)mount;
    if(strcmp(path,"/") && strcmp(path,"/mnt"))return -ENOENT;
    *node=(struct nas_fs_node){.is_dir=true,.mode=0040755};
    return 0;
}
static const struct nas_vfs_type root_type={
    .name="fixture-root",.lookup=root_lookup,.read_only=true
};
static const struct nas_vfs_mount root_mount={
    .mountpoint="/",.type=&root_type,.permanent=true
};
void nas_vfs_register_default_mounts(void) {
    assert(nas_vfs_register_mount(&root_mount)==0);
}
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *disk=fopen(argv[1],"rb");
    assert(disk);
    assert(!fseek(disk,0,SEEK_END));
    long n=ftell(disk);
    assert(n>1024*1024 && n<128*1024*1024);
    rewind(disk);
    uint8_t *image=malloc((size_t)n);
    assert(image);
    assert(fread(image,1,(size_t)n,disk)==(size_t)n);
    assert(!fclose(disk));
    static struct nas_lwext4_mount mounted;
    assert(nas_lwext4_ramdisk_mount(&mounted,"vfs-test-ram","vfs-test-ext",
                                    "/mnt",image,(uint64_t)n)==0);
    char actual[256],buffer[128]={0};
    struct nas_fs_node node;
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&node,actual)==0);
    assert(!strcmp(node.filesystem->name,"lwext4"));
    assert(!strcmp(actual,"/mnt/sandbox/hardlink"));
    assert(!node.is_dir && !node.is_symlink && !nas_vfs_can_write(&node));
    assert(node.ino>=11 && node.nlink>=1);
    assert(nas_vfs_node_device(&node)>=8);
    static const char expected[]="NaSeed VFS and BlockDevice real lwext4 data\n";
    assert(node.size==sizeof(expected)-1);
    assert(nas_vfs_read_at(&node,0,(uint8_t *)buffer,sizeof(buffer))==(int64_t)node.size);
    assert(!memcmp(expected,buffer,sizeof(expected)-1));
    assert(nas_vfs_read_at(&node,7,(uint8_t *)buffer,12)==12);
    assert(!memcmp(expected+7,buffer,12));
    assert(nas_vfs_read_at(&node,node.size,(uint8_t *)buffer,1)==0);
    struct nas_fs_node directory;
    assert(nas_fs_lookup_node("/mnt/sandbox",true,&directory,actual)==0);
    assert(directory.is_dir);
    struct nas_vfs_dirent entries[8]={0};
    int count=nas_vfs_read_dir(&directory,"/mnt/sandbox",0,entries,8);
    assert(count>=3); /* . .. hardlink, from actual ext4 */
    bool found=false;
    for(int i=0;i<count;i++)if(!strcmp(entries[i].name,"hardlink")) {
        assert(entries[i].ino==node.ino && entries[i].dtype==8);
        found=true;
    }
    assert(found);
    assert(nas_vfs_read_dir(&directory,"/mnt/sandbox",
                             (uint64_t)count,entries,8)>=0);
    assert(nas_vfs_read_dir(&node,"/mnt/sandbox",0,entries,8)==-ENOTDIR);
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink/",true,&node,actual)==-ENOTDIR);
    assert(nas_vfs_node_pin(&node)==0);
    assert(nas_lwext4_ramdisk_unmount(&mounted)==-EBUSY);
    assert(nas_vfs_node_unpin(&node)==0);
    struct nas_fs_context *cwd=nas_fs_context_new();
    assert(cwd);
    assert(nas_vfs_node_pin(&directory)==0);
    cwd->cwd_node=directory;
    cwd->cwd_pinned=true;
    memcpy(cwd->cwd,"/mnt/sandbox",sizeof("/mnt/sandbox"));
    struct nas_fs_context *forked=nas_fs_context_copy(cwd);
    assert(forked && forked->cwd_pinned);
    assert(nas_lwext4_ramdisk_unmount(&mounted)==-EBUSY);
    nas_fs_context_put(cwd);
    assert(nas_lwext4_ramdisk_unmount(&mounted)==-EBUSY);
    nas_fs_context_put(forked);
    assert(nas_lwext4_ramdisk_unmount(&mounted)==0);
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&node,actual)==-ENOENT);
    free(image);
    puts("[LWEXT4 VFS PASS] real registered mount, inode lookup/read, positional IO, RO policy, busy unmount");
    return 0;
}
