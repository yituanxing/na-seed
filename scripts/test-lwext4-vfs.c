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
    assert(argc==2 || argc==3);
    const bool write=argc==3 && !strcmp(argv[2],"write");
    const bool verify=argc==3 && !strcmp(argv[2],"verify");
    assert(argc==2 || write || verify);
    FILE *disk=fopen(argv[1],write?"rb+":"rb");
    assert(disk);
    assert(!fseek(disk,0,SEEK_END));
    long n=ftell(disk);
    assert(n>1024*1024 && n<128*1024*1024);
    rewind(disk);
    uint8_t *image=malloc((size_t)n);
    assert(image);
    assert(fread(image,1,(size_t)n,disk)==(size_t)n);
    static struct nas_lwext4_mount mounted;
    assert(nas_lwext4_ramdisk_mount_mode(&mounted,"vfs-test-ram","vfs-test-ext",
                                    "/mnt",image,(uint64_t)n,write)==0);
    char actual[256],buffer[128]={0};
    struct nas_fs_node node;
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&node,actual)==0);
    assert(!strcmp(node.filesystem->name,"lwext4"));
    assert(!strcmp(actual,"/mnt/sandbox/hardlink"));
    assert(!node.is_dir && !node.is_symlink && nas_vfs_can_write(&node)==write);
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
    assert(nas_vfs_mkdir("/",0755)==-EEXIST);
    assert(nas_vfs_mkdir("/mnt",0755)==-EEXIST);
    assert(nas_vfs_mkdir("/mnt/sandbox",0755)==-EEXIST);
    if(!write) {
        assert(nas_vfs_create("/mnt/sandbox/new-file",0644)==-EROFS);
        assert(nas_vfs_write_at(&node,0,(const uint8_t *)"x",1)==-EROFS);
        assert(nas_vfs_resize(&node,0)==-EROFS);
        assert(nas_vfs_mkdir("/mnt/create",0755)==-EROFS);
        assert(nas_vfs_unlink("/mnt/sandbox/hardlink",0)==-EROFS);
        assert(nas_vfs_rename("/mnt/sandbox/hardlink","/mnt/sandbox/new",0)==-EROFS);
        assert(nas_vfs_link("/mnt/sandbox/hardlink","/mnt/sandbox/alias")==-EROFS);
        assert(nas_vfs_symlink("hardlink","/mnt/sandbox/symlink")==-EROFS);
    }
    static const uint8_t persisted[]="NaSeed writable VFS ext4 inode storage";
    if(write) {
        assert(nas_vfs_create("/mnt/sandbox/vfs-created",0640)==0);
        assert(nas_vfs_create("/mnt/sandbox/vfs-created",0640)==-EEXIST);
        struct nas_fs_node created;
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&created,actual)==0);
        assert(nas_vfs_can_write(&created) && (created.mode&0777U)==0640U);
        assert(nas_vfs_write_at(&created,0,persisted,sizeof(persisted)-1)==(int64_t)sizeof(persisted)-1);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&created,actual)==0);
        assert(created.size==sizeof(persisted)-1);
        assert(nas_vfs_read_at(&created,0,(uint8_t *)buffer,sizeof(buffer))==(int64_t)created.size);
        assert(!memcmp(buffer,persisted,sizeof(persisted)-1));
        assert(nas_vfs_resize(&created,10)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&created,actual)==0 && created.size==10);
        assert(nas_vfs_resize(&created,sizeof(persisted)-1)==0);
        /* Upstream shrink-only truncate must NOT be misreported as growth.
         * Verify the real inode has already expanded with zero bytes
         * BEFORE restoring the known content through VFS write_at. */
        struct nas_fs_node grown;
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&grown,actual)==0);
        assert(grown.size==sizeof(persisted)-1);
        uint8_t zero_tail[sizeof(persisted)]={0};
        assert(nas_vfs_read_at(&grown,10,zero_tail,sizeof(persisted)-11)==(int64_t)sizeof(persisted)-11);
        for(unsigned i=0;i<sizeof(persisted)-11;i++)assert(zero_tail[i]==0);
        assert(nas_vfs_write_at(&created,10,persisted+10,sizeof(persisted)-11)==(int64_t)sizeof(persisted)-11);
        assert(nas_vfs_sync(&created)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&created,actual)==0);
        assert(created.size==sizeof(persisted)-1);
        /* Two independent pinned Files: the reader carries an OLD inode
         * snapshot but must observe writer growth, truncate and regrowth. */
        struct nas_fs_node stale_reader=created;
        assert(nas_vfs_node_pin(&created)==0);
        assert(nas_vfs_node_pin(&stale_reader)==0);
        assert(nas_vfs_node_size(&stale_reader)==sizeof(persisted)-1);
        assert(nas_vfs_resize(&created,6)==0);
        assert(nas_vfs_node_size(&stale_reader)==6);
        assert(nas_vfs_read_at(&stale_reader,6,(uint8_t *)buffer,1)==0);
        assert(nas_vfs_resize(&created,sizeof(persisted)-1)==0);
        assert(nas_vfs_write_at(&created,6,persisted+6,sizeof(persisted)-7)==(int64_t)sizeof(persisted)-7);
        assert(nas_vfs_node_size(&stale_reader)==sizeof(persisted)-1);
        assert(nas_vfs_read_at(&stale_reader,0,(uint8_t *)buffer,sizeof(persisted)-1)==(int64_t)sizeof(persisted)-1);
        assert(!memcmp(buffer,persisted,sizeof(persisted)-1));
        assert(nas_vfs_node_unpin(&stale_reader)==0);
        assert(nas_vfs_node_unpin(&created)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&created,actual)==0);
        assert(created.size==sizeof(persisted)-1);
        assert(nas_vfs_read_at(&created,0,(uint8_t *)buffer,created.size)==(int64_t)created.size);
        assert(!memcmp(buffer,persisted,created.size));
        assert(nas_vfs_mkdir("/mnt/sandbox/vfs-directory",0750)==0);
        assert(nas_vfs_create("/mnt/sandbox/vfs-directory/child",0600)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-directory",0x200ULL)==-ENOTEMPTY);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-directory/child",0)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-directory",0x200ULL)==0);
        assert(nas_vfs_symlink("vfs-created","/mnt/sandbox/vfs-link")==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-link",true,&created,actual)==0);
        assert(created.size==sizeof(persisted)-1);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-link",0)==0);
        assert(nas_vfs_link("/mnt/sandbox/vfs-created","/mnt/sandbox/vfs-alias")==0);
        assert(nas_vfs_rename("/mnt/sandbox/vfs-alias","/mnt/sandbox/vfs-persist",0)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-created",0)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-persist",true,&created,actual)==0);
        assert(nas_vfs_read_at(&created,0,(uint8_t *)buffer,sizeof(buffer))==(int64_t)created.size);
        assert(!memcmp(buffer,persisted,sizeof(persisted)-1));
    }
    if(verify) {
        struct nas_fs_node persisted_node;
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-persist",true,&persisted_node,actual)==0);
        assert(persisted_node.size==sizeof(persisted)-1);
        assert(nas_vfs_read_at(&persisted_node,0,(uint8_t *)buffer,sizeof(buffer))==(int64_t)persisted_node.size);
        assert(!memcmp(buffer,persisted,sizeof(persisted)-1));
    }
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
    /* Same authentic ext4 bytes, but this mount never creates a RamDisk.
     * The driver registers and owns its BlockDevice; the FS pins it and
     * the driver's premature detach MUST fail while mounted. This proves
     * a future VirtIO-Blk adapter needs zero ext4/VFS special cases. */
    struct nas_ramdisk external={0};
    assert(nas_ramdisk_attach(&external,"external-block-driver",image,(uint64_t)n,512,false)==0);
    static struct nas_lwext4_mount external_mount;
    assert(nas_lwext4_device_mount(&external_mount,"external-block-driver",
                                   "external-ext4-dev","/mnt",true)==-EROFS);
    assert(nas_lwext4_device_mount(&external_mount,"external-block-driver",
                                   "external-ext4-dev","/mnt",false)==0);
    assert(nas_ramdisk_detach(&external)==-EBUSY);
    struct nas_fs_node external_node;
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&external_node,actual)==0);
    assert(!strcmp(external_node.filesystem->name,"lwext4"));
    assert(nas_vfs_read_at(&external_node,0,(uint8_t *)buffer,sizeof(buffer))==(int64_t)external_node.size);
    assert(!memcmp(expected,buffer,sizeof(expected)-1));
    assert(nas_vfs_write_at(&external_node,0,(const uint8_t *)"x",1)==-EROFS);
    assert(nas_lwext4_unmount(&external_mount)==0);
    assert(nas_ramdisk_detach(&external)==0);
    if(write) {
        rewind(disk);
        assert(fwrite(image,1,(size_t)n,disk)==(size_t)n);
        assert(!fflush(disk));
    }
    assert(!fclose(disk));
    free(image);
    puts(write?"[LWEXT4 VFS RW PASS] actual generic VFS namespace, inode I/O, persistence":
         verify?"[LWEXT4 VFS REMOUNT PASS] persisted writable ext4 tree verified read-only":
         "[LWEXT4 VFS PASS] real registered mount, inode lookup/read, positional IO, RO policy, busy unmount");
    return 0;
}
