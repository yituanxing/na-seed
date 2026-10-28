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
    /* Upstream ext4_ftruncate is shrink-only: it returns EOK without
     * extending i_size. Linux ftruncate(grow) MUST expose zero-filled
     * bytes and the requested new length. Materialize zeros through the
     * same inode-relative upstream write API until sparse allocation is
     * provided by the filesystem; never lie to VFS about persisted size. */
    uint64_t old_size=nas_vfs_node_size(node);
    if(length<=old_size) {
        int rc=ext4_ftruncate(&f,length);
        return rc ? -rc : 0;
    }
    static const uint8_t zeros[1024]={0};
    uint64_t offset=old_size;
    while(offset<length) {
        uint64_t take=length-offset;
        if(take>sizeof(zeros))take=sizeof(zeros);
        ext4_file writer={.mp=ctx->upstream_mount,
            .inode=(uint32_t)(uintptr_t)node->inode,
            .fsize=old_size,.fpos=offset,.flags=O_WRONLY};
        size_t written=0;
        int rc=ext4_fwrite(&writer,zeros,(size_t)take,&written);
        if(rc)return -rc;
        if(!written)return -EIO;
        offset+=(uint64_t)written;
    }
    return 0;
}
/* Linux fsync durability ordering:
 *  1. write back dirty ext4 metadata/data through the BlockDevice bridge;
 *  2. issue the owning DRIVER's cache flush, not just an lwext4 buffer flush.
 * A transport that has no flush contract cannot claim a durable fsync.
 * Common VFS/ABI never enumerates RAM/PCI/ISA drivers. */
static int ext_sync_mount(struct nas_lwext4_mount *ctx) {
    if(!ctx || !ctx->mounted || !ctx->device.storage)return -ENODEV;
    int rc=ext4_cache_flush(ctx->upstream_prefix);
    if(rc)return -rc;
    return nas_block_flush(ctx->device.storage);
}
static int ext_sync_inode(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->mount->private_data)return -EINVAL;
    return ext_sync_mount(node->mount->private_data);
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
/* Discover the current *name* of a pinned ext4 directory by following
 * upstream on-disk '..' inode references, then looking up that child's
 * live name inside its parent. Only the filesystem owns this knowledge.
 * The VFS validates the resulting (mount, inode) before any pathname
 * operation; this adapter never manufactures inode identities or opens
 * an old path. Bounded by the common 256-byte pathname limit. */
static ext4_dir ext_handle(const struct nas_lwext4_mount *ctx,uint32_t ino) {
    ext4_dir dir={0};
    dir.f.mp=ctx->upstream_mount;
    dir.f.inode=ino;
    dir.f.flags=O_RDONLY;
    return dir;
}
static int ext_parent_directory(struct nas_lwext4_mount *ctx,
                                uint32_t child,uint32_t *parent) {
    ext4_dir dir=ext_handle(ctx,child);
    const ext4_direntry *ent;
    uint32_t found=0;
    while((ent=ext4_dir_entry_next(&dir))) {
        if(ent->inode && ent->name_length==2 &&
           ent->name[0]=='.' && ent->name[1]=='.') {
            found=ent->inode;
            break;
        }
    }
    int rc=ext4_dir_close(&dir);
    if(rc)return -rc;
    if(!found)return -ESTALE;
    *parent=found;
    return 0;
}
static int ext_name_in_directory(struct nas_lwext4_mount *ctx,
                                 uint32_t parent,uint32_t child,
                                 char name[256],unsigned *length) {
    ext4_dir dir=ext_handle(ctx,parent);
    const ext4_direntry *ent;
    unsigned used=0;
    while((ent=ext4_dir_entry_next(&dir))) {
        if(ent->inode!=child || !ent->name_length ||
           (ent->name_length==1 && ent->name[0]=='.') ||
           (ent->name_length==2 && ent->name[0]=='.' &&
            ent->name[1]=='.'))
            continue;
        used=ent->name_length;
        memcpy(name,ent->name,used);
        name[used]=0;
        break;
    }
    int rc=ext4_dir_close(&dir);
    if(rc)return -rc;
    if(!used)return -ESTALE;
    *length=used;
    return 0;
}
static int ext_directory_path(const struct nas_fs_node *node,
                              char absolute[256]) {
    if(!node || !node->is_dir || !node->mount || !absolute)
        return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx || !ctx->mounted || !ctx->upstream_mount ||
       !node->ino || node->ino>UINT32_MAX)return -ENODEV;
    uint32_t root=0;
    struct ext4_inode raw;
    int rc=ext4_raw_inode_fill(ctx->upstream_prefix,&root,&raw);
    if(rc)return -rc;
    if(!root)return -EIO;
    uint32_t current=(uint32_t)node->ino;
    char reverse[256];
    reverse[255]=0;
    unsigned cursor=255;
    /* Maximum number of components is at most 127 in a 255-byte
     * absolute path, including one-character components. */
    for(unsigned depth=0;depth<128 && current!=root;depth++) {
        uint32_t parent;
        rc=ext_parent_directory(ctx,current,&parent);
        if(rc)return rc;
        if(parent==current)return -ELOOP;
        char name[256];
        unsigned length=0;
        rc=ext_name_in_directory(ctx,parent,current,name,&length);
        if(rc)return rc;
        if(length+1>cursor)return -ENAMETOOLONG;
        cursor-=length;
        memcpy(reverse+cursor,name,length);
        reverse[--cursor]='/';
        current=parent;
    }
    if(current!=root)return -ELOOP;
    size_t base=strlen(ctx->mount_point);
    size_t relative=255U-cursor;
    if(base+relative>=256)return -ENAMETOOLONG;
    memcpy(absolute,ctx->mount_point,base);
    memcpy(absolute+base,reverse+cursor,relative+1);
    return 0;
}
/* POSIX open directory descriptors follow an inode across rename.
 * The pinned upstream ext4_dir_entry_next() itself only requires
 * dir.f.mp, dir.f.inode and dir.next_off; constructing that established
 * upstream handle from the live VFS inode avoids reopening a stale path.
 * The VFS's logical cursor remains owned by the shared open File.
 * The directory scanner currently traverses from offset zero per batch;
 * persistent byte-offset iterators are a separate optimization. */
static int ext_read_directory(const struct nas_fs_node *node,
                              const char *absolute,uint64_t cursor,
                              struct nas_vfs_dirent *out,unsigned maximum) {
    (void)absolute; /* namespace path may have been renamed after open */
    if(!node || !node->mount || !node->mount->private_data ||
       !out || !maximum)return -EINVAL;
    if(!node->is_dir)return -ENOTDIR;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    if(!node->ino || node->ino>UINT32_MAX)return -EIO;
    /* This is an actual liblwext4 directory handle, not an invented
     * directory reader: upstream ext4_dir_entry_next() resolves its
     * extents by the provided inode number, not by a filename. */
    ext4_dir dir={0};
    dir.f.mp=ctx->upstream_mount;
    dir.f.inode=(uint32_t)node->ino;
    dir.f.flags=O_RDONLY;
    dir.next_off=0;
    int error=0;
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
    .directory_path=ext_directory_path,
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
    bool no_flush=writable && (!block->ops || !block->ops->flush);
    nas_block_put(block);
    if(forbidden)return -EROFS;
    /* Fail closed rather than mounting a writable, apparently 'durable'
     * filesystem over a transport that cannot confirm a cache flush. */
    if(no_flush)return -EOPNOTSUPP;
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
    /* A failed physical flush must leave mount and inode ownership
     * untouched. Never unregister the VFS mount before checking durability. */
    int rc=ext_sync_mount(ctx);
    if(rc)return rc;
    rc=nas_vfs_unregister_mount(&ctx->mount);
    if(rc)return rc;
    int e=ext4_umount(ctx->upstream_prefix);
    if(e) {
        (void)nas_vfs_register_mount(&ctx->mount);
        return -e;
    }
    /* Upstream ext4_umount updates on-disk superblock/counters AFTER the
     * preceding data-cache flush. Flush its final metadata writes through
     * the actual storage controller before releasing BlockDevice ownership. */
    int final_sync=nas_block_flush(ctx->device.storage);
    /* ext4_umount has already finalized its superblock. On a final hardware
     * flush error the VFS namespace is gone: returning here would leak a
     * permanently pinned BlockDevice with mounted=false and no retry path.
     * Finish all adapter/owned-RAM cleanup and propagate the durability
     * error, never claim fsync success or remount a finalized superblock. */
    ctx->mounted=false;
    rc=nas_lwext4_device_detach(&ctx->device,ctx->device_name);
    if(ctx->owns_ramdisk) {
        int ram_rc=nas_ramdisk_detach(&ctx->ramdisk);
        if(!rc)rc=ram_rc;
        if(!ram_rc)ctx->owns_ramdisk=false;
    }
    ctx->upstream_mount=NULL;ctx->upstream_super=NULL;
    return final_sync ? final_sync : rc;
}
int nas_lwext4_ramdisk_unmount(struct nas_lwext4_mount *ctx) {
    return nas_lwext4_unmount(ctx);
}
