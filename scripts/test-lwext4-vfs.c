/* The real VFS namei -> lwext4 inode -> BlockDevice/RamDisk integration
 * against the host-created authentic ext4 image; not a guest syscall test. */
#include "../kernel/fs/lookup.h"
#include "../kernel/fs/vfs.h"
#include "../ports/lwext4/guest_vfs.h"
#include "../kernel/block/device.h"
#include "../kernel/block/partition.h"
#include "../kernel/errno.h"
#include "../kernel/task/resources.h"
#include <ext4.h>
#include <ext4_inode.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Traced driver backend: identical registered BlockDevice contract as any
 * VirtIO/PCI transport, with real ext4 bytes and fault-injectable hardware
 * cache completion. A successful lwext4 metadata flush alone is not fsync. */
struct traced_disk {
    struct nas_block_device dev;
    uint8_t *image;
    unsigned reads,writes,flushes;
    bool fail_flush;
};
static int trace_read(struct nas_block_device *dev,uint64_t sector,
                      uint32_t count,void *destination) {
    struct traced_disk *t=dev->context;
    ++t->reads;
    memcpy(destination,t->image+sector*dev->sector_bytes,
           (size_t)count*dev->sector_bytes);
    return 0;
}
static int trace_write(struct nas_block_device *dev,uint64_t sector,
                       uint32_t count,const void *source) {
    struct traced_disk *t=dev->context;
    ++t->writes;
    memcpy(t->image+sector*dev->sector_bytes,source,
           (size_t)count*dev->sector_bytes);
    return 0;
}
static int trace_flush(struct nas_block_device *dev) {
    struct traced_disk *t=dev->context;
    ++t->flushes;
    return t->fail_flush ? -EIO : 0;
}
static const struct nas_block_ops trace_ops={
    .read=trace_read,.write=trace_write,.flush=trace_flush
};
static const struct nas_block_ops trace_no_flush_ops={
    .read=trace_read,.write=trace_write
};
void nas_print(const char *s) { fputs(s,stderr); }
void nas_arch_halt(void) { abort(); }
static int root_lookup(const struct nas_vfs_mount *mount,const char *path,
                       struct nas_fs_node *node) {
    (void)mount;
    if(strcmp(path,"/") && strcmp(path,"/mnt"))return -ENOENT;
    static const uint8_t root_inode_identity,covered_inode_identity;
    const bool covered=!strcmp(path,"/mnt");
    *node=(struct nas_fs_node){.is_dir=true,.mode=0040755,
        .ino=covered?2:1,
        .inode=covered?(const void *)&covered_inode_identity:
                       (const void *)&root_inode_identity};
    return 0;
}
static int root_get(const struct nas_vfs_mount *m,struct nas_fs_node *out) {
    return root_lookup(m,"/",out);
}
static int root_child(const struct nas_fs_node *parent,const char *name,
                      struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    return !strcmp(name,"mnt") ?
        root_lookup(parent->mount,"/mnt",out) : -ENOENT;
}
static const struct nas_vfs_type root_type={
    .name="fixture-root",.root=root_get,.lookup_child=root_child,
    .read_only=true
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
    char stale_orphan_path[256]={0},stale_directory_orphan_path[256]={0};
    static struct nas_lwext4_mount mounted;
    assert(nas_lwext4_ramdisk_mount_mode(&mounted,"vfs-test-ram","vfs-test-ext",
                                    "/mnt",image,(uint64_t)n,write)==0);
    char actual[256],buffer[128]={0};
    /* Private orphan staging is NEVER an accessible or listed user inode
     * even when writable mount initialisation had to create the directory. */
    struct nas_fs_node hidden,root_dir;
    assert(nas_fs_lookup_node("/mnt/.naseed-orphans",true,
                              &hidden,actual)==-ENOENT);
    assert(nas_fs_lookup_node("/mnt",true,&root_dir,actual)==0);
    struct nas_vfs_dirent root_entries[16];
    uint64_t root_cursor=0;
    for(unsigned iteration=0;iteration<256;iteration++) {
        int got=nas_vfs_read_dir(&root_dir,root_cursor,root_entries,16);
        assert(got>=0);
        if(!got)break;
        for(int i=0;i<got;i++)
            assert(strcmp(root_entries[i].name,".naseed-orphans"));
        root_cursor+=(uint64_t)got;
        assert(iteration<255);
    }
    struct nas_fs_node node;
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&node,actual)==0);
    assert(!strcmp(node.filesystem->name,"lwext4"));
    assert(!strcmp(actual,"/mnt/sandbox/hardlink"));
    assert(!node.is_dir && !node.is_symlink && nas_vfs_can_write(&node)==write);
    assert(node.ino>=11 && node.nlink>=1);
    assert(nas_vfs_node_device(&node)>=8);
    assert(node.mount!=&mounted.mount);
    assert(!strcmp(node.mount->mountpoint,"/mnt"));
    assert(node.mount->private_data==&mounted);
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
    int count=nas_vfs_read_dir(&directory,0,entries,8);
    assert(count>=3); /* . .. hardlink, from actual ext4 */
    bool found=false;
    for(int i=0;i<count;i++)if(!strcmp(entries[i].name,"hardlink")) {
        assert(entries[i].ino==node.ino && entries[i].dtype==8);
        found=true;
    }
    assert(found);
    assert(nas_vfs_read_dir(&directory,
                             (uint64_t)count,entries,8)>=0);
    assert(nas_vfs_read_dir(&node,0,entries,8)==-ENOTDIR);
    struct nas_fs_node candidate_parent;
    char candidate[256];
    assert(nas_fs_lookup_parent("/mnt/sandbox/negative-candidate",
                    &candidate_parent,candidate)==0);
    assert(candidate_parent.mount==node.mount &&
           !strcmp(candidate,"/mnt/sandbox/negative-candidate"));
    assert(nas_fs_canonical_candidate("/mnt/sandbox/negative-candidate",
                                      candidate)==0);
    assert(!strcmp(candidate,"/mnt/sandbox/negative-candidate"));
    assert(nas_fs_canonical_candidate("/mnt/sandbox/negative-candidate///",
                                      candidate)==0);
    assert(!strcmp(candidate,"/mnt/sandbox/negative-candidate"));
    assert(nas_fs_canonical_candidate("/mnt/sandbox/hardlink/",
                                      candidate)==-ENOTDIR);
    assert(nas_fs_canonical_candidate(
               "/mnt/sandbox/hardlink/../negative-candidate",
               candidate)==-ENOTDIR);
    assert(nas_vfs_mkdir("/",0755)==-EEXIST);
    assert(nas_vfs_mkdir("/mnt",0755)==-EEXIST);
    assert(nas_vfs_unlink("/mnt",0x200ULL)==-EBUSY);
    assert(nas_vfs_unlink("/mnt",0x800ULL)==-EINVAL);
    assert(nas_vfs_link("/mnt/sandbox","/mnt/sandbox/dir-hardlink")==-EPERM);
    assert(nas_vfs_rename("/mnt","/new-mount",0)==-EBUSY);
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
        /* Real upstream metadata mutations must reach fstat on an OLD,
         * independently pinned inode snapshot. The ext4 VFS adapter does
         * not currently expose chmod/utimens callbacks, so exercise the
         * pinned upstream setters on the same mounted ext4 image. */
        assert(ext4_mode_set("/mnt/sandbox/vfs-created",0604)==0);
        assert(ext4_atime_set("/mnt/sandbox/vfs-created",1770000001U)==0);
        assert(ext4_mtime_set("/mnt/sandbox/vfs-created",1770000002U)==0);
        assert(ext4_ctime_set("/mnt/sandbox/vfs-created",1770000003U)==0);
        struct nas_fs_node live_stat;
        assert(nas_vfs_getattr(&stale_reader,&live_stat)==0);
        assert(live_stat.ino==stale_reader.ino && live_stat.nlink==1);
        assert((live_stat.mode&07777U)==0604U);
        assert(live_stat.atime_sec==1770000001U);
        assert(live_stat.mtime_sec==1770000002U);
        assert(live_stat.ctime_sec==1770000003U);
        assert(live_stat.size==sizeof(persisted)-1);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-created",true,&grown,actual)==0);
        assert(grown.mode==live_stat.mode && grown.mtime_sec==live_stat.mtime_sec);
        /* Actual original liblwext4 unlink-open: the LAST name is moved
         * into a hidden orphan, not ext4_fremove()'d until final close.
         * This is independent of the persistent vfs-created file below. */
        assert(nas_vfs_create("/mnt/sandbox/orphan-test",0600)==0);
        struct nas_fs_node orphaned;
        assert(nas_fs_lookup_node("/mnt/sandbox/orphan-test",true,
                                  &orphaned,actual)==0);
        static const uint8_t old_data[]="open-old-inode-keeps-data";
        assert(nas_vfs_node_pin(&orphaned)==0);
        assert(nas_vfs_node_pin(&orphaned)==0);
        assert(nas_vfs_write_at(&orphaned,0,old_data,
                                sizeof(old_data)-1)==(int64_t)sizeof(old_data)-1);
        assert(nas_vfs_unlink("/mnt/sandbox/orphan-test",0)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/orphan-test",true,
                                  &hidden,actual)==-ENOENT);
        struct nas_fs_node orphan_stat;
        assert(nas_vfs_getattr(&orphaned,&orphan_stat)==0 &&
               orphan_stat.nlink==0);
        assert(nas_vfs_read_at(&orphaned,0,(uint8_t *)buffer,
                               sizeof(old_data)-1)==(int64_t)sizeof(old_data)-1);
        assert(!memcmp(buffer,old_data,sizeof(old_data)-1));
        /* New file at the recycled pathname is a DIFFERENT inode; both
         * independent open descriptions can survive until their own close. */
        assert(nas_vfs_create("/mnt/sandbox/orphan-test",0644)==0);
        struct nas_fs_node replacement;
        assert(nas_fs_lookup_node("/mnt/sandbox/orphan-test",true,
                                  &replacement,actual)==0);
        assert(replacement.ino!=orphaned.ino);
        assert(nas_vfs_node_pin(&replacement)==0);
        assert(nas_vfs_write_at(&replacement,0,(const uint8_t *)"new",3)==3);
        assert(nas_vfs_write_at(&orphaned,sizeof(old_data)-1,
                                (const uint8_t *)"+append",7)==7);
        assert(nas_vfs_node_size(&orphaned)==sizeof(old_data)-1+7);
        assert(nas_vfs_getattr(&orphaned,&orphan_stat)==0);
        assert(orphan_stat.nlink==0 &&
               orphan_stat.size==sizeof(old_data)-1+7 &&
               (orphan_stat.mode&07777U)==0600U);
        assert(nas_vfs_node_unpin(&orphaned)==0);
        assert(nas_vfs_read_at(&orphaned,0,(uint8_t *)buffer,
                               sizeof(old_data)-1)==(int64_t)sizeof(old_data)-1);
        assert(nas_vfs_node_unpin(&orphaned)==0);
        assert(nas_vfs_read_at(&replacement,0,(uint8_t *)buffer,3)==3);
        assert(!memcmp(buffer,"new",3));
        assert(nas_vfs_node_unpin(&replacement)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/orphan-test",0)==0);
        assert(nas_fs_lookup_node("/mnt/.naseed-orphans",true,
                                  &hidden,actual)==-ENOENT);
        assert(nas_vfs_read_at(&stale_reader,0,(uint8_t *)buffer,
                               sizeof(persisted)-1)==(int64_t)sizeof(persisted)-1);
        assert(!memcmp(buffer,persisted,sizeof(persisted)-1));
        /* Separate open descriptions have independent stale snapshots:
         * the second writer MUST use the shared live inode size, not its
         * original fsize when appending data. Test authentic ext4 bytes. */
        static const uint8_t appended[]="+second-open";
        assert(nas_vfs_write_at(&stale_reader,sizeof(persisted)-1,
                                appended,sizeof(appended)-1)==
                                (int64_t)sizeof(appended)-1);
        assert(nas_vfs_node_size(&created)==
               sizeof(persisted)-1+sizeof(appended)-1);
        assert(nas_vfs_read_at(&created,sizeof(persisted)-1,
                               (uint8_t *)buffer,sizeof(appended)-1)==
                               (int64_t)sizeof(appended)-1);
        assert(!memcmp(buffer,appended,sizeof(appended)-1));
        assert(nas_vfs_resize(&created,sizeof(persisted)-1)==0);
        assert(nas_vfs_resize(&created,6)==0);
        assert(nas_vfs_node_size(&stale_reader)==6);
        assert(nas_vfs_getattr(&stale_reader,&live_stat)==0);
        assert(live_stat.size==6 && (live_stat.mode&07777U)==0604U);
        assert(nas_vfs_read_at(&stale_reader,6,(uint8_t *)buffer,1)==0);
        assert(nas_vfs_resize(&created,sizeof(persisted)-1)==0);
        assert(nas_vfs_write_at(&created,6,persisted+6,sizeof(persisted)-7)==(int64_t)sizeof(persisted)-7);
        assert(nas_vfs_node_size(&stale_reader)==sizeof(persisted)-1);
        assert(nas_vfs_getattr(&stale_reader,&live_stat)==0);
        assert(live_stat.size==sizeof(persisted)-1);
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
        /* Open directory descriptors follow inode identity across rename,
         * even if the caller still supplies its now-stale open pathname. */
        struct nas_fs_node open_directory;
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-directory",true,
                                  &open_directory,actual)==0);
        assert(nas_vfs_node_pin(&open_directory)==0);
        /* Own the REAL positive Dentry and its mount, not merely the
         * backend inode. Rename must update its parent/name in place. */
        struct nas_vfs_path live_dir={0},duplicate_dir={0};
        assert(nas_vfs_path_bind(&open_directory,
                                 "/mnt/sandbox/vfs-directory",
                                 &live_dir)==0);
        assert(nas_vfs_path_get(&live_dir,&duplicate_dir)==0);
        assert(nas_vfs_mkdir("/mnt/sandbox/vfs-directory/nested",0700)==0);
        struct nas_fs_node nested_directory;
        struct nas_vfs_path nested_path={0};
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-directory/nested",
                                  true,&nested_directory,actual)==0);
        assert(nas_vfs_path_bind(&nested_directory,actual,&nested_path)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-directory",0x200ULL)==-ENOTEMPTY);
        assert(nas_vfs_rename("/mnt/sandbox/vfs-directory",
                              "/mnt/sandbox/renamed-directory",0)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-directory",true,
                                  &directory,actual)==-ENOENT);
        char dentry_absolute[256];
        assert(nas_vfs_path_string(&live_dir,dentry_absolute)==0);
        assert(!strcmp(dentry_absolute,"/mnt/sandbox/renamed-directory"));
        assert(nas_vfs_path_string(&duplicate_dir,dentry_absolute)==0);
        assert(!strcmp(dentry_absolute,"/mnt/sandbox/renamed-directory"));
        assert(nas_vfs_path_string(&nested_path,dentry_absolute)==0);
        assert(!strcmp(dentry_absolute,"/mnt/sandbox/renamed-directory/nested"));
        struct nas_vfs_dirent renamed_entries[8]={0};
        int renamed_n=nas_vfs_read_dir(&open_directory,0,renamed_entries,8);
        assert(renamed_n>=3);
        bool child_found=false;
        for(int k=0;k<renamed_n;k++)
            if(!strcmp(renamed_entries[k].name,"child"))child_found=true;
        assert(child_found);
        char oldpath_base[256];
        /* The owned VFS Dentry follows the backend rename without the
         * now-deleted provider reverse-directory-path callback. */
        assert(nas_vfs_path_string(&live_dir,oldpath_base)==0);
        assert(!strcmp(oldpath_base,"/mnt/sandbox/renamed-directory"));
        assert(nas_vfs_mkdir("/mnt/sandbox/vfs-directory",0700)==0);
        struct nas_fs_node reused_directory;
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-directory",true,
                                  &reused_directory,actual)==0);
        assert(reused_directory.ino!=open_directory.ino);
        assert(nas_vfs_path_string(&live_dir,oldpath_base)==0);
        assert(!strcmp(oldpath_base,"/mnt/sandbox/renamed-directory"));
        assert(nas_vfs_path_string(&nested_path,dentry_absolute)==0);
        assert(!strcmp(dentry_absolute,"/mnt/sandbox/renamed-directory/nested"));
        assert(nas_vfs_path_put(&duplicate_dir)==0);
        assert(nas_vfs_node_unpin(&open_directory)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-directory",0x200ULL)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/renamed-directory/nested",
                              0x200ULL)==0);
        /* Unlinked directory remains a pinned inode, but can no longer
         * provide a live name to getcwd/openat from its obsolete Dentry. */
        assert(nas_vfs_path_string(&nested_path,dentry_absolute)==-ENOENT);
        assert(nas_vfs_path_put(&nested_path)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/renamed-directory/child",0)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/renamed-directory",0x200ULL)==0);
        assert(nas_vfs_path_string(&live_dir,dentry_absolute)==-ENOENT);
        assert(nas_vfs_path_put(&live_dir)==0);
        /* Open but empty directory: rmdir detaches the visible name while
         * preserving this pinned inode; later path reuse is a new object. */
        assert(nas_vfs_mkdir("/mnt/sandbox/open-rmdir",0700)==0);
        struct nas_fs_node opened_rmdir;
        assert(nas_fs_lookup_node("/mnt/sandbox/open-rmdir",true,
                                   &opened_rmdir,actual)==0);
        assert(nas_vfs_node_pin(&opened_rmdir)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/open-rmdir",0x200ULL)==0);
        struct nas_fs_node removed_dir;
        assert(nas_fs_lookup_node("/mnt/sandbox/open-rmdir",true,
                                   &removed_dir,actual)==-ENOENT);
        assert(nas_vfs_getattr(&opened_rmdir,&removed_dir)==0);
        assert(removed_dir.nlink==0 && removed_dir.ino==opened_rmdir.ino);
        struct nas_vfs_dirent old_dir_entries[8]={0};
        assert(nas_vfs_read_dir(&opened_rmdir,0,old_dir_entries,8)>=2);
        assert(!strcmp(old_dir_entries[0].name,"."));
        assert(nas_vfs_mkdir("/mnt/sandbox/open-rmdir",0700)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/open-rmdir",true,
                                   &removed_dir,actual)==0);
        assert(removed_dir.ino!=opened_rmdir.ino);
        assert(nas_vfs_node_unpin(&opened_rmdir)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/open-rmdir",0x200ULL)==0);
        /* Linux rename(replace) of EMPTY directories: non-empty target
         * fails without mutating either tree; valid replacement moves
         * source directory and its children under one existing name. */
        assert(nas_vfs_mkdir("/mnt/sandbox/move-tree",0700)==0);
        assert(nas_vfs_mkdir("/mnt/sandbox/move-tree/sub",0700)==0);
        assert(nas_vfs_create("/mnt/sandbox/move-tree/sub/payload",0600)==0);
        assert(nas_vfs_mkdir("/mnt/sandbox/move-target",0700)==0);
        assert(nas_vfs_create("/mnt/sandbox/move-target/occupied",0600)==0);
        assert(nas_vfs_rename("/mnt/sandbox/move-tree",
                              "/mnt/sandbox/move-target",0)==-ENOTEMPTY);
        assert(nas_fs_lookup_node("/mnt/sandbox/move-tree/sub/payload",
                                   true,&directory,actual)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/move-target/occupied",
                                   true,&directory,actual)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/move-target/occupied",0)==0);
        /* Parent-inode ancestry blocks a directory moved into itself;
         * no backend should need a separate unsafe lexical cycle test. */
        assert(nas_vfs_rename("/mnt/sandbox/move-tree",
                              "/mnt/sandbox/move-tree/sub/cycle",0)==-EINVAL);
        assert(nas_vfs_rename("/mnt/sandbox/move-tree",
                              "/mnt/sandbox/move-target",0)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/move-tree/sub/payload",
                                   true,&directory,actual)==-ENOENT);
        assert(nas_fs_lookup_node("/mnt/sandbox/move-target/sub/payload",
                                   true,&directory,actual)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/move-target/sub/payload",0)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/move-target/sub",0x200ULL)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/move-target",0x200ULL)==0);
        /* An already OPEN destination directory survives rename-overwrite
         * until final VFS unpin; do not recursively delete the victim. */
        assert(nas_vfs_mkdir("/mnt/sandbox/open-dir-source",0700)==0);
        assert(nas_vfs_mkdir("/mnt/sandbox/open-dir-victim",0700)==0);
        struct nas_fs_node open_victim_dir;
        assert(nas_fs_lookup_node("/mnt/sandbox/open-dir-victim",true,
                                   &open_victim_dir,actual)==0);
        assert(nas_vfs_node_pin(&open_victim_dir)==0);
        assert(nas_vfs_rename("/mnt/sandbox/open-dir-source",
                              "/mnt/sandbox/open-dir-victim",0)==0);
        struct nas_fs_node new_dir;
        assert(nas_fs_lookup_node("/mnt/sandbox/open-dir-source",true,
                                   &new_dir,actual)==-ENOENT);
        assert(nas_fs_lookup_node("/mnt/sandbox/open-dir-victim",true,
                                   &new_dir,actual)==0);
        assert(new_dir.ino!=open_victim_dir.ino);
        struct nas_fs_node victim_meta;
        assert(nas_vfs_getattr(&open_victim_dir,&victim_meta)==0);
        assert(victim_meta.nlink==0);
        struct nas_vfs_dirent orphan_entries[8]={0};
        assert(nas_vfs_read_dir(&open_victim_dir,0,orphan_entries,8)>=2);
        assert(!strcmp(orphan_entries[0].name,"."));
        assert(nas_vfs_node_unpin(&open_victim_dir)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/open-dir-victim",0x200ULL)==0);
        /* Full VFS flag handling, not a separate NoReplace compiler path. */
        assert(nas_vfs_create("/mnt/sandbox/noreplace-source",0600)==0);
        assert(nas_vfs_create("/mnt/sandbox/noreplace-target",0600)==0);
        assert(nas_vfs_rename("/mnt/sandbox/noreplace-source",
                              "/mnt/sandbox/noreplace-target",1)==-EEXIST);
        assert(nas_fs_lookup_node("/mnt/sandbox/noreplace-source",true,
                                   &new_dir,actual)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/noreplace-target",0)==0);
        assert(nas_vfs_rename("/mnt/sandbox/noreplace-source",
                              "/mnt/sandbox/noreplace-target",1)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/noreplace-target",0)==0);
        /* Real ext4 replacement: victim is OPEN and must retain its
         * original contents and inode number while the source atomically
         * becomes the visible target in our single-CPU VFS namespace. */
        assert(nas_vfs_create("/mnt/sandbox/rename-victim",0600)==0);
        struct nas_fs_node victim;
        assert(nas_fs_lookup_node("/mnt/sandbox/rename-victim",true,
                                  &victim,actual)==0);
        static const uint8_t victim_bytes[]="unmodified-open-victim";
        assert(nas_vfs_write_at(&victim,0,victim_bytes,
                                sizeof(victim_bytes)-1)==
                                (int64_t)sizeof(victim_bytes)-1);
        assert(nas_fs_lookup_node("/mnt/sandbox/rename-victim",true,
                                  &victim,actual)==0);
        assert(nas_vfs_node_pin(&victim)==0);
        assert(nas_vfs_create("/mnt/sandbox/rename-source",0600)==0);
        struct nas_fs_node rename_source;
        assert(nas_fs_lookup_node("/mnt/sandbox/rename-source",true,
                                  &rename_source,actual)==0);
        static const uint8_t fresh_bytes[]="new visible contents";
        assert(nas_vfs_write_at(&rename_source,0,fresh_bytes,
                                sizeof(fresh_bytes)-1)==
                                (int64_t)sizeof(fresh_bytes)-1);
        assert(nas_vfs_rename("/mnt/sandbox/rename-source",
                              "/mnt/sandbox/rename-victim",0)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/rename-source",true,
                                  &rename_source,actual)==-ENOENT);
        assert(nas_fs_lookup_node("/mnt/sandbox/rename-victim",true,
                                  &rename_source,actual)==0);
        assert(rename_source.ino!=victim.ino);
        assert(nas_vfs_read_at(&rename_source,0,(uint8_t *)buffer,
                               sizeof(fresh_bytes)-1)==
                               (int64_t)sizeof(fresh_bytes)-1);
        assert(!memcmp(buffer,fresh_bytes,sizeof(fresh_bytes)-1));
        struct nas_fs_node victim_stat;
        assert(nas_vfs_getattr(&victim,&victim_stat)==0 &&
               victim_stat.nlink==0);
        assert(nas_vfs_read_at(&victim,0,(uint8_t *)buffer,
                               sizeof(victim_bytes)-1)==
                               (int64_t)sizeof(victim_bytes)-1);
        assert(!memcmp(buffer,victim_bytes,sizeof(victim_bytes)-1));
        assert(nas_vfs_node_unpin(&victim)==0);
        /* Replacing an unpinned destination also works; vendor's rename
         * otherwise returns EEXIST even without an open victim. */
        assert(nas_vfs_create("/mnt/sandbox/replacement-no-open",0600)==0);
        assert(nas_vfs_rename("/mnt/sandbox/rename-victim",
                             "/mnt/sandbox/replacement-no-open",0)==0);
        assert(nas_fs_lookup_node("/mnt/sandbox/rename-victim",true,
                                  &rename_source,actual)==-ENOENT);
        assert(nas_fs_lookup_node("/mnt/sandbox/replacement-no-open",true,
                                  &rename_source,actual)==0);
        assert(nas_vfs_read_at(&rename_source,0,(uint8_t *)buffer,
                               sizeof(fresh_bytes)-1)==
                               (int64_t)sizeof(fresh_bytes)-1);
        assert(!memcmp(buffer,fresh_bytes,sizeof(fresh_bytes)-1));
        assert(nas_vfs_unlink("/mnt/sandbox/replacement-no-open",0)==0);
        assert(nas_vfs_symlink("vfs-created","/mnt/sandbox/vfs-link")==0);
        struct nas_fs_node opened_link;
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-link",false,
                                  &opened_link,actual)==0);
        assert(nas_vfs_node_pin(&opened_link)==0);
        char link_target[256];
        assert(nas_vfs_readlink(&opened_link,link_target)==11);
        assert(!strcmp(link_target,"vfs-created"));
        /* A symlink is inode DATA, not a pathname: rename the name while
         * keeping the old inode reference open. */
        assert(nas_vfs_rename("/mnt/sandbox/vfs-link",
                              "/mnt/sandbox/vfs-link-moved",0)==0);
        assert(nas_vfs_readlink(&opened_link,link_target)==11);
        assert(!strcmp(link_target,"vfs-created"));
        assert(nas_fs_lookup_node("/mnt/sandbox/vfs-link-moved",
                                  true,&created,actual)==0);
        assert(created.size==sizeof(persisted)-1);
        assert(nas_vfs_node_unpin(&opened_link)==0);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-link-moved",0)==0);
        assert(nas_vfs_link("/mnt/sandbox/vfs-created","/mnt/sandbox/vfs-alias")==0);
        /* Link count is another live inode field: an open File must not
         * report the value captured before link/rename/unlink. */
        struct nas_fs_node linked_hold=created;
        assert(nas_vfs_node_pin(&linked_hold)==0);
        struct nas_fs_node linked_stat;
        assert(nas_vfs_getattr(&linked_hold,&linked_stat)==0 &&
               linked_stat.nlink==2);
        assert(nas_vfs_rename("/mnt/sandbox/vfs-alias","/mnt/sandbox/vfs-persist",0)==0);
        assert(nas_vfs_getattr(&linked_hold,&linked_stat)==0 &&
               linked_stat.nlink==2);
        assert(nas_vfs_unlink("/mnt/sandbox/vfs-created",0)==0);
        assert(nas_vfs_getattr(&linked_hold,&linked_stat)==0 &&
               linked_stat.nlink==1);
        assert(nas_vfs_node_unpin(&linked_hold)==0);
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
    if(write) {
        /* Simulate sudden power loss between final user unlink and
         * last File release: use the REAL original upstream rename to
         * leave one fully linked orphan on disk without a live VFS record.
         * This tests next mount recovery, rather than mocking cleanup. */
        assert(nas_vfs_create("/mnt/sandbox/crashed-open-file",0600)==0);
        struct nas_fs_node abandoned;
        assert(nas_fs_lookup_node("/mnt/sandbox/crashed-open-file",true,
                                  &abandoned,actual)==0);
        static const uint8_t abandoned_data[]="must be reaped on remount";
        assert(nas_vfs_write_at(&abandoned,0,abandoned_data,
                                sizeof(abandoned_data)-1)==
                                (int64_t)sizeof(abandoned_data)-1);
        assert(snprintf(stale_orphan_path,sizeof(stale_orphan_path),
                 "/mnt/.naseed-orphans/o%08x-fffffffe",
                 (unsigned)abandoned.ino)>0);
        assert(!ext4_frename("/mnt/sandbox/crashed-open-file",
                             stale_orphan_path));
        /* A second independently crashed private directory orphan
         * exercises empty-dir cleanup without touching foreign entries. */
        assert(nas_vfs_mkdir("/mnt/sandbox/crashed-empty-directory",0700)==0);
        struct nas_fs_node abandoned_directory;
        assert(nas_fs_lookup_node("/mnt/sandbox/crashed-empty-directory",
                                   true,&abandoned_directory,actual)==0);
        assert(snprintf(stale_directory_orphan_path,
                 sizeof(stale_directory_orphan_path),
                 "/mnt/.naseed-orphans/d%08x-fffffffd",
                 (unsigned)abandoned_directory.ino)>0);
        assert(!ext4_dir_mv("/mnt/sandbox/crashed-empty-directory",
                             stale_directory_orphan_path));
        uint32_t orphan_number=0;struct ext4_inode orphan_raw;
        assert(!ext4_raw_inode_fill(stale_orphan_path,
                                     &orphan_number,&orphan_raw));
        assert(orphan_number==abandoned.ino);
        assert(nas_fs_lookup_node(stale_orphan_path,true,
                                  &abandoned,actual)==-ENOENT);
    }
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink/",true,&node,actual)==-ENOTDIR);
    assert(nas_vfs_node_pin(&node)==0);
    assert(nas_lwext4_ramdisk_unmount(&mounted)==-EBUSY);
    assert(nas_vfs_node_unpin(&node)==0);
    struct nas_fs_context *cwd=nas_fs_context_new();
    assert(cwd);
    struct nas_fs_node stable_cwd;
    assert(nas_fs_lookup_node("/mnt/sandbox",true,
                              &stable_cwd,actual)==0);
    assert(nas_vfs_node_pin(&stable_cwd)==0);
    cwd->cwd_node=stable_cwd;
    cwd->cwd_pinned=true;
    memcpy(cwd->cwd,"/mnt/sandbox",sizeof("/mnt/sandbox"));
    assert(nas_vfs_path_bind(&stable_cwd,"/mnt/sandbox",
                             &cwd->cwd_path)==0);
    struct nas_fs_context *forked=nas_fs_context_copy(cwd);
    assert(forked && forked->cwd_pinned);
    assert(forked->cwd_path.dentry==cwd->cwd_path.dentry);
    char cloned_cwd[256];
    assert(nas_vfs_path_string(&forked->cwd_path,cloned_cwd)==0);
    assert(!strcmp(cloned_cwd,"/mnt/sandbox"));
    assert(nas_lwext4_ramdisk_unmount(&mounted)==-EBUSY);
    nas_fs_context_put(cwd);
    /* Forked independent FS context still owns the exact same Dentry,
     * inode and mount identity until its final independently held put. */
    assert(nas_vfs_path_string(&forked->cwd_path,cloned_cwd)==0);
    assert(!strcmp(cloned_cwd,"/mnt/sandbox"));
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
    if(write) {
        uint32_t still_present=0;struct ext4_inode raw;
        assert(!ext4_raw_inode_fill(stale_orphan_path,
                                     &still_present,&raw));
        assert(still_present!=0); /* RO mount never silently mutates disk */
        uint32_t dir_remains=0;
        assert(!ext4_raw_inode_fill(stale_directory_orphan_path,
                                    &dir_remains,&raw));
        assert(dir_remains!=0);
    }
    assert(nas_ramdisk_detach(&external)==-EBUSY);
    struct nas_fs_node external_node;
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,&external_node,actual)==0);
    assert(!strcmp(external_node.filesystem->name,"lwext4"));
    assert(nas_vfs_read_at(&external_node,0,(uint8_t *)buffer,sizeof(buffer))==(int64_t)external_node.size);
    assert(!memcmp(expected,buffer,sizeof(expected)-1));
    assert(nas_vfs_write_at(&external_node,0,(const uint8_t *)"x",1)==-EROFS);
    assert(nas_lwext4_unmount(&external_mount)==0);
    assert(nas_ramdisk_detach(&external)==0);
    /* Real original ext4 mounted from a nonzero-LBA child BlockDevice.
     * No special-case offset logic in vendor lwext4 or shared VFS. */
    const uint64_t partition_start=8;
    const uint64_t disk_bytes=(uint64_t)n+partition_start*512U;
    uint8_t *partitioned_disk=malloc((size_t)disk_bytes);
    assert(partitioned_disk);
    memset(partitioned_disk,0,(size_t)partition_start*512U);
    memcpy(partitioned_disk+(size_t)partition_start*512U,image,(size_t)n);
    struct nas_ramdisk whole_disk={0};
    struct nas_partition_view partition={0};
    struct nas_partition_extent partition_range={
        .first_lba=partition_start,.sectors=(uint64_t)n/512U
    };
    assert(nas_ramdisk_attach(&whole_disk,"partition-whole-disk",
                               partitioned_disk,disk_bytes,512,false)==0);
    assert(nas_partition_attach(&partition,"partition-ext4",
                                 "partition-whole-disk",
                                 &partition_range,false)==0);
    static struct nas_lwext4_mount partition_mount;
    assert(nas_lwext4_device_mount(&partition_mount,"partition-ext4",
                                   "partition-ext4-dev","/mnt",false)==0);
    assert(nas_ramdisk_detach(&whole_disk)==-EBUSY);
    assert(nas_partition_detach(&partition)==-EBUSY);
    assert(nas_fs_lookup_node("/mnt/sandbox/hardlink",true,
                              &external_node,actual)==0);
    assert(nas_vfs_read_at(&external_node,0,(uint8_t *)buffer,
                           sizeof(buffer))==(int64_t)external_node.size);
    assert(!memcmp(expected,buffer,sizeof(expected)-1));
    assert(nas_lwext4_unmount(&partition_mount)==0);
    assert(nas_partition_detach(&partition)==0);
    assert(nas_ramdisk_detach(&whole_disk)==0);
    free(partitioned_disk);
    puts("[LWEXT4 PARTITION PASS] real upstream ext4 from generic nonzero-LBA BlockDevice view");
    if(write) {
        /* Physical durability contract: an externally driver-owned,
         * writable block device with an explicit, fault-injectable flush.
         * Never substitute a VFS marker for the real upstream filesystem. */
        struct traced_disk trace={.image=image};
        trace.dev=(struct nas_block_device){
            .name="traced-disk-driver",.sectors=(uint64_t)n/512U,
            .sector_bytes=512,.max_sectors_per_io=16,
            .ops=&trace_ops,.context=&trace
        };
        assert(nas_block_register(&trace.dev)==0);
        static struct nas_lwext4_mount traced_mount;
        trace.dev.ops=&trace_no_flush_ops;
        assert(nas_lwext4_device_mount(&traced_mount,"traced-disk-driver",
                                       "traced-ext4","/mnt",true)==-EOPNOTSUPP);
        trace.dev.ops=&trace_ops;
        assert(nas_lwext4_device_mount(&traced_mount,"traced-disk-driver",
                                       "traced-ext4","/mnt",true)==0);
        uint32_t recovered=0;struct ext4_inode raw_orphan;
        assert(ext4_raw_inode_fill(stale_orphan_path,
                                    &recovered,&raw_orphan)==ENOENT);
        assert(ext4_raw_inode_fill(stale_directory_orphan_path,
                                    &recovered,&raw_orphan)==ENOENT);
        assert(nas_fs_lookup_node("/mnt/.naseed-orphans",true,
                                  &hidden,actual)==-ENOENT);
        assert(nas_block_unregister(&trace.dev)==-EBUSY);
        assert(nas_vfs_create("/mnt/sandbox/durable-file",0600)==0);
        struct nas_fs_node durable;
        assert(nas_fs_lookup_node("/mnt/sandbox/durable-file",true,&durable,actual)==0);
        static const uint8_t data[]="driver-flushed ext4, not just VFS buffered\n";
        assert(nas_vfs_write_at(&durable,0,data,sizeof(data)-1)==(int64_t)sizeof(data)-1);
        assert(trace.reads>0 && trace.writes>0);
        trace.fail_flush=true;
        unsigned before=trace.flushes;
        assert(nas_vfs_sync(&durable)==-EIO);
        assert(trace.flushes==before+1);
        /* A rejected final flush must NOT orphan a still-live mount. */
        assert(nas_lwext4_unmount(&traced_mount)==-EIO);
        assert(nas_fs_lookup_node("/mnt/sandbox/durable-file",true,&durable,actual)==0);
        trace.fail_flush=false;
        assert(nas_vfs_sync(&durable)==0);
        assert(trace.flushes>=before+3);
        assert(nas_lwext4_unmount(&traced_mount)==0);
        assert(nas_block_unregister(&trace.dev)==0);
        /* Fresh read-only mount after actual physical flush must recover
         * the bytes, proving that a failed flush did not fake persistence. */
        assert(nas_block_register(&trace.dev)==0);
        static struct nas_lwext4_mount traced_verify;
        assert(nas_lwext4_device_mount(&traced_verify,"traced-disk-driver",
                                       "traced-verify","/mnt",false)==0);
        struct nas_fs_node restored;
        assert(nas_fs_lookup_node("/mnt/sandbox/durable-file",true,&restored,actual)==0);
        assert(restored.size==sizeof(data)-1);
        assert(nas_vfs_read_at(&restored,0,(uint8_t *)buffer,sizeof(buffer))==
               (int64_t)sizeof(data)-1);
        assert(!memcmp(data,buffer,sizeof(data)-1));
        assert(nas_lwext4_unmount(&traced_verify)==0);
        assert(nas_block_unregister(&trace.dev)==0);
        puts("[LWEXT4 DRIVER FLUSH PASS] real filesystem fsync/umount propagate physical EIO, remount verified");
    }
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
