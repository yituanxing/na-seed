/* Real upstream ext4 inode -> NaSeed VFS, independent of physical drivers.
 * Ext4 inode numbers (not pathname lookups) identify open files for reads. */
#include "guest_vfs.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <ext4.h>
#include <ext4_inode.h>
#include <ext4_types.h>
#include <stddef.h>
#include <stdint.h>
static const char *path_for(const struct nas_lwext4_mount *m,
                            const char *absolute) {
    return !strcmp(absolute,m->mount_point) ?
           m->upstream_prefix : absolute;
}
static int ext_lookup(const struct nas_vfs_mount *vfs,
                      const char *absolute,struct nas_fs_node *out) {
    if(!vfs || !vfs->private_data || !absolute || !out)return -EINVAL;
    struct nas_lwext4_mount *ctx=vfs->private_data;
    if(!ctx->mounted)return -ENODEV;
    struct ext4_inode raw;
    uint32_t number=0;
    int e=ext4_raw_inode_fill(path_for(ctx,absolute),&number,&raw);
    if(e)return -e;
    if(!number)return -EIO;
    uint32_t mode=ext4_inode_get_mode(ctx->upstream_super,&raw);
    *out=(struct nas_fs_node){
        .inode=(const void *)(uintptr_t)number,
        .ino=number,.nlink=ext4_inode_get_links_cnt(&raw),
        .uid=ext4_inode_get_uid(&raw),.gid=ext4_inode_get_gid(&raw),
        .atime_sec=ext4_inode_get_access_time(&raw),
        .mtime_sec=ext4_inode_get_modif_time(&raw),
        .ctime_sec=ext4_inode_get_change_inode_time(&raw),
        .mode=mode,
        .size=ext4_inode_get_size(ctx->upstream_super,&raw),
        .is_dir=(mode & 0170000U)==0040000U,
        .is_symlink=(mode & 0170000U)==0120000U
    };
    return 0;
}
static int ext_readlink_node(const struct nas_fs_node *node,
                             const char *absolute,char target[256]) {
    if(!node || !node->mount || !node->mount->private_data ||
       !absolute || !target)return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    size_t length=0;
    int e=ext4_readlink(path_for(ctx,absolute),target,255,&length);
    if(e)return -e;
    if(length>=256)return -ENAMETOOLONG;
    target[length]=0;
    return (int)length;
}
static int64_t ext_read_inode(const struct nas_fs_node *node,
                              uint64_t offset,uint8_t *dst,uint64_t length) {
    if(!node || !node->mount || !node->mount->private_data)return -EINVAL;
    if(node->is_dir)return -EISDIR;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    if(!length)return 0;
    if(!dst || offset>INT64_MAX)return -EINVAL;
    /* Read against live mount+inode size, NOT the pathname lookup snapshot:
     * a second independently opened File may have grown or truncated it. */
    uint64_t size=nas_vfs_node_size(node);
    if(offset>=size)return 0;
    if(length>size-offset)length=size-offset;
    if(length>SIZE_MAX)length=SIZE_MAX;
    /* ext4_fread uses the live inode ID and upstream mount. The temporary
     * handle is positional: no shared File offset and NO reopening a path
     * after rename. This is safe while the VFS File pins its mount. */
    ext4_file file={
        .mp=ctx->upstream_mount,
        .inode=(uint32_t)(uintptr_t)node->inode,
        .fsize=size,.fpos=offset,.flags=O_RDONLY
    };
    size_t obtained=0;
    int e=ext4_fread(&file,dst,(size_t)length,&obtained);
    return e ? -e : (int64_t)obtained;
}
/* Inode-relative write: no pathname reopen after rename. The VFS enforces
 * per-mount write policy. Guest boot remains read-only by default. */
static int64_t ext_write_inode(const struct nas_fs_node *node,uint64_t offset,
                               const uint8_t *src,uint64_t length) {
    if(!node || !node->mount || !node->mount->private_data)return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    if(node->is_dir)return -EISDIR;
    if(!src && length)return -EFAULT;
    if(length>SIZE_MAX || offset>INT64_MAX)return -EFBIG;
    ext4_file file={
        .mp=ctx->upstream_mount,.inode=(uint32_t)(uintptr_t)node->inode,
        .fsize=node->size,.fpos=offset,.flags=O_WRONLY
    };
    size_t written=0;
    int rc=ext4_fwrite(&file,src,(size_t)length,&written);
    return rc ? -rc : (int64_t)written;
}
static int ext_resize_inode(const struct nas_fs_node *node,uint64_t length) {
    if(!node || !node->mount || !node->mount->private_data)return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    ext4_file f={.mp=ctx->upstream_mount,
                 .inode=(uint32_t)(uintptr_t)node->inode,
                 .fsize=node->size,.flags=O_RDWR};
    int rc=ext4_ftruncate(&f,length);
    return rc ? -rc : 0;
}
static int ext_sync_inode(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->mount->private_data)return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted)return -ENODEV;
    int rc=ext4_cache_flush(ctx->upstream_prefix);
    return rc ? -rc : 0;
}
static int ext_create_path(const struct nas_vfs_mount *mount,const char *path,uint64_t mode) {
    (void)mount;
    ext4_file f={0};
    int rc=ext4_fopen(&f,path,"w");
    if(rc)return -rc;
    int close_rc=ext4_fclose(&f);
    if(close_rc)return -close_rc;
    rc=ext4_mode_set(path,(uint32_t)mode&07777U);
    return rc ? -rc : 0;
}
static int ext_mkdir_path(const struct nas_vfs_mount *mount,const char *path,uint64_t mode) {
    (void)mount;
    int rc=ext4_dir_mk(path);
    if(rc)return -rc;
    rc=ext4_mode_set(path,(uint32_t)mode&07777U);
    return rc ? -rc : 0;
}
static int ext_unlink_path(const struct nas_vfs_mount *mount,const char *path,uint64_t flags) {
    (void)mount;
    if(flags&~0x200ULL)return -EINVAL; /* AT_REMOVEDIR */
    if(!(flags&0x200ULL))return -ext4_fremove(path);
    /* Upstream ext4_dir_rm is recursive; never expose that to rmdir.
     * Reject any child except dot/dotdot under single-CPU VFS policy. */
    ext4_dir dir={0};
    int rc=ext4_dir_open(&dir,path);
    if(rc)return -rc;
    bool nonempty=false;
    const ext4_direntry *e;
    while((e=ext4_dir_entry_next(&dir))) {
        if(!e->inode)continue;
        if(e->name_length==1 && e->name[0]=='.')continue;
        if(e->name_length==2 && e->name[0]=='.' && e->name[1]=='.')continue;
        nonempty=true;break;
    }
    rc=ext4_dir_close(&dir);
    if(rc)return -rc;
    return nonempty ? -ENOTEMPTY : -ext4_dir_rm(path);
}
static int ext_rename_path(const struct nas_vfs_mount *mount,
                            const char *from,const char *to,uint64_t flags) {
    if(flags)return -EINVAL; /* no unimplemented atomic rename flags */
    if(!mount || !mount->private_data)return -ENODEV;
    struct nas_lwext4_mount *ctx=mount->private_data;
    if(!ctx->mounted || !ctx->upstream_super)return -ENODEV;
    struct ext4_inode inode;
    uint32_t number=0;
    int rc=ext4_raw_inode_fill(from,&number,&inode);
    if(rc)return -rc;
    if(!number)return -EIO;
    uint32_t mode=ext4_inode_get_mode(ctx->upstream_super,&inode);
    return (mode&0170000U)==0040000U ?
           -ext4_dir_mv(from,to) : -ext4_frename(from,to);
}
static int ext_link_path(const struct nas_vfs_mount *mount,const char *from,const char *to) {
    (void)mount;
    return -ext4_flink(from,to);
}
static int ext_symlink_path(const struct nas_vfs_mount *mount,const char *target,const char *path) {
    (void)mount;
    return -ext4_fsymlink(target,path);
}
/* A bounded directory view through upstream lwext4. Each call
 * reopens and scans the directory; later mutable dirs need owned iterator
 * state per shared File, but no backend-specific syscall ABI dispatch. */
static int ext_read_directory(const struct nas_fs_node *node,
                              const char *absolute,uint64_t cursor,
                              struct nas_vfs_dirent *out,unsigned maximum) {
    if(!node || !node->mount || !node->mount->private_data ||
       !absolute || !out || !maximum)return -EINVAL;
    if(!node->is_dir)return -ENOTDIR;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted)return -ENODEV;
    ext4_dir dir={0};
    int error=ext4_dir_open(&dir,path_for(ctx,absolute));
    if(error)return -error;
    unsigned copied=0;
    uint64_t scanned=0;
    const ext4_direntry *entry;
    while((entry=ext4_dir_entry_next(&dir))) {
        if(!entry->inode)continue;
        if(scanned++<cursor)continue;
        struct nas_vfs_dirent *dst=&out[copied];
        if(!entry->name_length) {
            error=EIO;break;
        }
        dst->ino=entry->inode;
        dst->name_len=entry->name_length;
        memcpy(dst->name,entry->name,entry->name_length);
        dst->name[entry->name_length]=0;
        switch(entry->inode_type) {
            case EXT4_DE_REG_FILE: dst->dtype=8;break;
            case EXT4_DE_DIR: dst->dtype=4;break;
            case EXT4_DE_CHRDEV: dst->dtype=2;break;
            case EXT4_DE_BLKDEV: dst->dtype=6;break;
            case EXT4_DE_FIFO: dst->dtype=1;break;
            case EXT4_DE_SOCK: dst->dtype=12;break;
            case EXT4_DE_SYMLINK: dst->dtype=10;break;
            default: dst->dtype=0;break;
        }
        if(++copied==maximum)break;
    }
    int close_error=ext4_dir_close(&dir);
    if(error)return -error;
    if(close_error)return -close_error;
    return (int)copied;
}
static const struct nas_vfs_type ext_type={
    .name="lwext4",
    .exec_source_label="lwext4",
    .lookup=ext_lookup,
    .readlink=ext_readlink_node,
    .read_at=ext_read_inode,
    .write_at=ext_write_inode,
    .resize=ext_resize_inode,.sync=ext_sync_inode,
    .read_dir=ext_read_directory,
    .create=ext_create_path,.mkdir=ext_mkdir_path,
    .unlink=ext_unlink_path,.rename=ext_rename_path,
    .link=ext_link_path,.symlink=ext_symlink_path,
    /* Capability is mount-specific; default guest mount remains RO. */
    .read_only=false
};
static bool copy_name(char *out,size_t capacity,const char *src) {
    if(!src || !src[0])return false;
    size_t length=strlen(src);
    if(length>=capacity)return false;
    memcpy(out,src,length+1);
    return true;
}
/* Generic FS mount consumes a previously registered BlockDevice (e.g.
 * VirtIO-Blk, NVMe or RAM). It never owns, probes or dereferences any
 * driver's storage. The device remains pinned until final unmount. */
int nas_lwext4_device_mount(struct nas_lwext4_mount *ctx,
                            const char *block_name,const char *dev_name,
                            const char *mount_point,bool writable) {
    if(!ctx || ctx->mounted || ctx->device.registered)return -EINVAL;
    struct nas_block_device *block=nas_block_get(block_name);
    if(!block)return -ENODEV;
    bool forbidden=writable && block->read_only;
    nas_block_put(block);
    if(forbidden)return -EROFS;
    if(!copy_name(ctx->block_name,sizeof(ctx->block_name),block_name) ||
       !copy_name(ctx->device_name,sizeof(ctx->device_name),dev_name) ||
       !copy_name(ctx->mount_point,sizeof(ctx->mount_point),mount_point))
        return -ENAMETOOLONG;
    size_t length=strlen(ctx->mount_point);
    if(ctx->mount_point[0]!='/' || length<2 ||
       ctx->mount_point[length-1]=='/' || length+2>sizeof(ctx->upstream_prefix))
        return -EINVAL;
    memcpy(ctx->upstream_prefix,ctx->mount_point,length);
    ctx->upstream_prefix[length]='/';
    ctx->upstream_prefix[length+1]=0;
    int rc=nas_lwext4_device_attach(&ctx->device,ctx->block_name,ctx->device_name);
    if(rc)return rc;
    rc=ext4_mount(ctx->device_name,ctx->upstream_prefix,!writable);
    if(rc) {rc=-rc;goto FailDevice;}
    rc=ext4_get_sblock(ctx->upstream_prefix,&ctx->upstream_super);
    if(rc) {rc=-rc;goto FailMount;}
    ext4_dir dir={0};
    rc=ext4_dir_open(&dir,ctx->upstream_prefix);
    if(rc) {rc=-rc;goto FailMount;}
    ctx->upstream_mount=dir.f.mp;
    rc=ext4_dir_close(&dir);
    if(rc) {rc=-rc;goto FailMount;}
    if(!ctx->upstream_mount) {rc=-EIO;goto FailMount;}
    ctx->mount=(struct nas_vfs_mount){
        .mountpoint=ctx->mount_point,
        .type=&ext_type,
        .read_only=!writable,
        .private_data=ctx
    };
    ctx->mounted=true; /* lookup may run during VFS mount registration */
    rc=nas_vfs_register_mount(&ctx->mount);
    if(!rc)return 0;
    ctx->mounted=false;
FailMount:
    (void)ext4_umount(ctx->upstream_prefix);
FailDevice:
    (void)nas_lwext4_device_detach(&ctx->device,ctx->device_name);
    ctx->upstream_mount=NULL;
    ctx->upstream_super=NULL;
    return rc;
}
/* RAM-backed boot-module path is an adapter, not the only mount path.
 * The root mount/unmount logic above/below does not know a driver kind. */
int nas_lwext4_ramdisk_mount_mode(struct nas_lwext4_mount *ctx,
                             const char *block_name,const char *dev_name,
                             const char *mount_point,void *image,
                             uint64_t image_bytes,bool writable) {
    if(!ctx || !image || ctx->mounted || ctx->ramdisk.block.registered)
        return -EINVAL;
    int rc=nas_ramdisk_attach(&ctx->ramdisk,block_name,image,image_bytes,512,writable);
    if(rc)return rc;
    rc=nas_lwext4_device_mount(ctx,block_name,dev_name,mount_point,writable);
    if(rc) {
        (void)nas_ramdisk_detach(&ctx->ramdisk);
        return rc;
    }
    ctx->owns_ramdisk=true;
    return 0;
}
int nas_lwext4_ramdisk_mount(struct nas_lwext4_mount *ctx,
                             const char *block_name,const char *dev_name,
                             const char *mount_point,void *image,
                             uint64_t image_bytes) {
    return nas_lwext4_ramdisk_mount_mode(ctx,block_name,dev_name,
                                        mount_point,image,image_bytes,false);
}
int nas_lwext4_unmount(struct nas_lwext4_mount *ctx) {
    if(!ctx || !ctx->mounted)return -EINVAL;
    int rc=nas_vfs_unregister_mount(&ctx->mount);
    if(rc)return rc;
    int e=ext4_umount(ctx->upstream_prefix);
    if(e) {
        (void)nas_vfs_register_mount(&ctx->mount);
        return -e;
    }
    ctx->mounted=false;
    rc=nas_lwext4_device_detach(&ctx->device,ctx->device_name);
    if(rc)return rc;
    if(ctx->owns_ramdisk) {
        rc=nas_ramdisk_detach(&ctx->ramdisk);
        if(rc)return rc;
        ctx->owns_ramdisk=false;
    }
    ctx->upstream_mount=NULL;ctx->upstream_super=NULL;
    return 0;
}
int nas_lwext4_ramdisk_unmount(struct nas_lwext4_mount *ctx) {
    return nas_lwext4_unmount(ctx);
}
