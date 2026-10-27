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
    if(offset>=node->size || !length)return 0;
    if(!dst || offset>INT64_MAX)return -EINVAL;
    if(length>node->size-offset)length=node->size-offset;
    if(length>SIZE_MAX)length=SIZE_MAX;
    /* ext4_fread uses the live inode ID and upstream mount. The temporary
     * handle is positional: no shared File offset and NO reopening a path
     * after rename. This is safe while the VFS File pins its mount. */
    ext4_file file={
        .mp=ctx->upstream_mount,
        .inode=(uint32_t)(uintptr_t)node->inode,
        .fsize=node->size,.fpos=offset,.flags=O_RDONLY
    };
    size_t obtained=0;
    int e=ext4_fread(&file,dst,(size_t)length,&obtained);
    return e ? -e : (int64_t)obtained;
}
static const struct nas_vfs_type ext_type={
    .name="lwext4",
    .exec_source_label="lwext4",
    .lookup=ext_lookup,
    .readlink=ext_readlink_node,
    .read_at=ext_read_inode,
    .read_only=true /* No generic namespace/write contract exposed yet. */
};
static bool copy_name(char *out,size_t capacity,const char *src) {
    if(!src || !src[0])return false;
    size_t length=strlen(src);
    if(length>=capacity)return false;
    memcpy(out,src,length+1);
    return true;
}
int nas_lwext4_ramdisk_mount(struct nas_lwext4_mount *ctx,
                             const char *block_name,const char *dev_name,
                             const char *mount_point,void *image,
                             uint64_t image_bytes) {
    if(!ctx || !image || ctx->mounted)return -EINVAL;
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
    int rc=nas_ramdisk_attach(&ctx->ramdisk,ctx->block_name,image,
                              image_bytes,512,false);
    if(rc)return rc;
    rc=nas_lwext4_device_attach(&ctx->device,ctx->block_name,ctx->device_name);
    if(rc)goto FailDisk;
    rc=ext4_mount(ctx->device_name,ctx->upstream_prefix,true);
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
        .read_only=true,
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
FailDisk:
    (void)nas_ramdisk_detach(&ctx->ramdisk);
    ctx->upstream_mount=NULL;
    ctx->upstream_super=NULL;
    return rc;
}
int nas_lwext4_ramdisk_unmount(struct nas_lwext4_mount *ctx) {
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
    rc=nas_ramdisk_detach(&ctx->ramdisk);
    ctx->upstream_mount=NULL;ctx->upstream_super=NULL;
    return rc;
}
