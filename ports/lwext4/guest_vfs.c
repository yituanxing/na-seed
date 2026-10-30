/* Real upstream ext4 inode -> NaSeed VFS, independent of physical drivers.
 * Ext4 inode numbers (not pathname lookups) identify open files for reads. */
#include "guest_vfs.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <ext4.h>
#include <ext4_fs.h>
#include <ext4_inode.h>
#include <ext4_types.h>
#include <stddef.h>
#include <stdint.h>
#define EXT4_ORPHAN_DIRECTORY ".naseed-orphans"
static const char orphan_digits[]="0123456789abcdef";
/* Directory offsets are disposable accelerator hints only. A failed VFS
 * mutation may have partly changed vendor state, so invalidate BEFORE it. */
static void ext_namespace_changed(struct nas_lwext4_mount *ctx) {
    if(!ctx)return;
    if(++ctx->namespace_epoch==0) {
        ctx->directory_hint=(struct nas_lwext4_dir_hint){0};
        ctx->namespace_epoch=1;
    }
}
static bool ext_reserved_root(const struct nas_lwext4_mount *ctx,
                              uint64_t parent,const char *name) {
    return ctx && parent==ctx->root_ino &&
           !strcmp(name,EXT4_ORPHAN_DIRECTORY);
}
static void ext_orphan_name(char name[19],uint32_t ino,uint32_t serial,
                            bool is_dir) {
    name[0]=is_dir?'d':'o';name[9]='-';name[18]=0;
    for(unsigned i=0;i<8;i++) {
        name[1+i]=orphan_digits[(ino>>(28-4*i))&15U];
        name[10+i]=orphan_digits[(serial>>(28-4*i))&15U];
    }
}
static bool ext_orphan_record(const uint8_t *name,unsigned n) {
    if(n!=18 || (name[0]!='o' && name[0]!='d') ||
       name[9]!='-')return false;
    for(unsigned i=1;i<18;i++) {
        if(i==9)continue;
        if(!((name[i]>='0' && name[i]<='9') ||
             (name[i]>='a' && name[i]<='f')))return false;
    }
    return true;
}
static int ext_orphan_path(const struct nas_lwext4_mount *ctx,
                           uint32_t ino,uint32_t serial,bool is_dir,
                           char path[256]) {
    char name[19];
    ext_orphan_name(name,ino,serial,is_dir);
    return nas_vfs_join_child(ctx->orphan_dir,name,path);
}
/* A directory entry already supplies the true inode number. Read metadata
 * through the SAME original ext4_fs_get_inode_ref API used by getattr;
 * do not reconstruct a pathname and ask upstream to walk it again. This
 * also keeps all inode identity independent of rename/hardlink aliases. */
static int ext_snapshot_inode(struct nas_lwext4_mount *ctx,uint32_t ino,
                              struct nas_fs_node *out) {
    if(!ctx || !ctx->mounted || !ctx->device.ext.fs || !ctx->upstream_super ||
       !ino || !out)return -ENODEV;
    struct ext4_inode_ref ref={0};
    int rc=ext4_fs_get_inode_ref(ctx->device.ext.fs,ino,&ref);
    if(rc)return -rc;
    struct ext4_inode *raw=ref.inode;
    uint32_t mode=ext4_inode_get_mode(ctx->upstream_super,raw);
    *out=(struct nas_fs_node){
        .inode=(const void *)(uintptr_t)ino,
        .ino=ino,.nlink=ext4_inode_get_links_cnt(raw),
        .uid=ext4_inode_get_uid(raw),.gid=ext4_inode_get_gid(raw),
        .atime_sec=ext4_inode_get_access_time(raw),
        .mtime_sec=ext4_inode_get_modif_time(raw),
        .ctime_sec=ext4_inode_get_change_inode_time(raw),
        .mode=mode,.size=ext4_inode_get_size(ctx->upstream_super,raw),
        .is_dir=(mode&0170000U)==0040000U,
        .is_symlink=(mode&0170000U)==0120000U
    };
    rc=ext4_fs_put_inode_ref(&ref);
    return rc ? -rc : 0;
}
static int ext_lookup_root(const struct nas_vfs_mount *vfs,
                           struct nas_fs_node *out) {
    if(!vfs || !vfs->private_data || !out)return -EINVAL;
    struct nas_lwext4_mount *ctx=vfs->private_data;
    return ext_snapshot_inode(ctx,ctx->root_ino,out);
}
/* ext4 lookup is now directly beneath a directory inode. Upstream ext4
 * owns directory iteration and inode loading; the VFS never passes a
 * reconstructed absolute pathname to this callback. */
static int ext_directory_path(const struct nas_fs_node *node,char absolute[256]);
static int ext_lookup_child(const struct nas_fs_node *parent,const char *name,
                            struct nas_fs_node *out) {
    if(!parent || !parent->is_dir || !parent->mount || !name || !name[0] ||
       !out || !parent->ino || parent->ino>UINT32_MAX)return -ENOTDIR;
    struct nas_lwext4_mount *ctx=parent->mount->private_data;
    if(!ctx || !ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    if(ext_reserved_root(ctx,parent->ino,name))return -ENOENT;
    ext4_dir dir={0};
    dir.f.mp=ctx->upstream_mount;
    dir.f.inode=(uint32_t)parent->ino;
    dir.f.flags=O_RDONLY;
    uint32_t number=0;
    size_t len=strlen(name);
    if(len>255)return -ENAMETOOLONG;
    const ext4_direntry *ent;
    while((ent=ext4_dir_entry_next(&dir))) {
        if(ent->inode && ent->name_length==len &&
           !memcmp(ent->name,name,len)) {
            number=ent->inode;
            break;
        }
    }
    int rc=ext4_dir_close(&dir);
    if(rc)return -rc;
    if(!number)return -ENOENT;
    return ext_snapshot_inode(ctx,number,out);
}
static int ext_readlink_node(const struct nas_fs_node *node,char target[256]) {
    if(!node || !node->mount || !node->mount->private_data ||
       !target)return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    if(!node->ino || node->ino>UINT32_MAX)return -ESTALE;
    if((node->mode&0170000U)!=0120000U)return -EINVAL;
    /* Original lwext4 ext4_fread accepts a known inode and handles both
     * inline (<60 byte) and block-backed symlink storage. ext4_readlink
     * instead reopens a GLOBAL pathname and would fail after rename, or
     * worse follow a recycled name. Do not modify the vendor library. */
    if(node->size>=256)return -ENAMETOOLONG;
    ext4_file f={.mp=ctx->upstream_mount,.inode=(uint32_t)node->ino,
                 .fsize=node->size,.fpos=0,.flags=O_RDONLY};
    size_t length=0;
    int e=ext4_fread(&f,target,255,&length);
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
        .fsize=nas_vfs_node_size(node),.fpos=offset,.flags=O_WRONLY
    };
    size_t written=0;
    int rc=ext4_fwrite(&file,src,(size_t)length,&written);
    return rc ? -rc : (int64_t)written;
}
static int ext_resize_inode(const struct nas_fs_node *node,uint64_t length) {
    if(!node || !node->mount || !node->mount->private_data)return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount)return -ENODEV;
    uint64_t old_size=nas_vfs_node_size(node);
    ext4_file f={.mp=ctx->upstream_mount,
                 .inode=(uint32_t)(uintptr_t)node->inode,
                 .fsize=old_size,.flags=O_RDWR};
    /* Upstream ext4_ftruncate is shrink-only: it returns EOK without
     * extending i_size. Linux ftruncate(grow) MUST expose zero-filled
     * bytes and the requested new length. Materialize zeros through the
     * same inode-relative upstream write API until sparse allocation is
     * provided by the filesystem; never lie to VFS about persisted size. */
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
            .fsize=offset,.fpos=offset,.flags=O_WRONLY};
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
/* Upstream lwext4 namespace APIs are path-oriented. Keep that limitation
 * strictly inside this port: generate the path from the LIVE parent inode,
 * not a global namei string supplied by the kernel or a stale File hint. */
static int ext_child_path(const struct nas_fs_node *parent,const char *name,
                           char path[256]) {
    if(!parent || !parent->mount || !parent->is_dir || !name ||
       !name[0] || strchr(name,'/'))return -EINVAL;
    const struct nas_lwext4_mount *ctx=parent->mount->private_data;
    if(ext_reserved_root(ctx,parent->ino,name))return -EPERM;
    int rc=ext_directory_path(parent,path);
    if(rc)return rc;
    char child[256];
    rc=nas_vfs_join_child(path,name,child);
    if(rc)return rc;
    memcpy(path,child,strlen(child)+1);
    return 0;
}
static int ext_create_path(const struct nas_fs_node *parent,const char *name,uint64_t mode) {
    char path[256];int rc=ext_child_path(parent,name,path);
    if(rc)return rc;
    ext_namespace_changed(parent->mount->private_data);
    ext4_file f={0};
    rc=ext4_fopen(&f,path,"w");
    if(rc)return -rc;
    int close_rc=ext4_fclose(&f);
    if(close_rc)return -close_rc;
    rc=ext4_mode_set(path,(uint32_t)mode&07777U);
    return rc ? -rc : 0;
}
static int ext_mkdir_path(const struct nas_fs_node *parent,const char *name,uint64_t mode) {
    char path[256];int rc=ext_child_path(parent,name,path);
    if(rc)return rc;
    ext_namespace_changed(parent->mount->private_data);
    rc=ext4_dir_mk(path);
    if(rc)return -rc;
    rc=ext4_mode_set(path,(uint32_t)mode&07777U);
    return rc ? -rc : 0;
}
/* The upstream liblwext4 namespace API is NOT a POSIX replacement
 * transaction: it refuses an occupied target, and removes last-linked
 * inodes immediately. Retain a private typed dentry for every live inode
 * that loses its final visible name. Everything stays in this adapter. */
static int ext_dir_empty(const char *path) {
    ext4_dir dir={0};
    int rc=ext4_dir_open(&dir,path);
    if(rc)return -rc;
    bool nonempty=false;
    const ext4_direntry *e;
    while((e=ext4_dir_entry_next(&dir))) {
        if(!e->inode)continue;
        if(e->name_length==1 && e->name[0]=='.')continue;
        if(e->name_length==2 && e->name[0]=='.' && e->name[1]=='.')
            continue;
        nonempty=true;break;
    }
    rc=ext4_dir_close(&dir);
    if(rc)return -rc;
    return nonempty?-ENOTEMPTY:0;
}
/* Never use upstream ext4_dir_rm without the empty-directory preflight:
 * that operation may recursively remove an entire directory tree. */
static int ext_remove_staged(const char *path,bool is_dir) {
    if(!is_dir)return -ext4_fremove(path);
    int rc=ext_dir_empty(path);
    return rc?rc:-ext4_dir_rm(path);
}
static int ext_move_staged(const char *from,const char *to,bool is_dir) {
    return is_dir ? -ext4_dir_mv(from,to) : -ext4_frename(from,to);
}
static int ext_stage_orphan(struct nas_lwext4_mount *ctx,
                            const char *from,uint32_t ino,bool is_dir) {
    if(!ctx || !ctx->mounted || !ctx->orphan_dir[0])return -EIO;
    unsigned slot=NAS_LWEXT4_ORPHAN_LIMIT;
    for(unsigned i=0;i<NAS_LWEXT4_ORPHAN_LIMIT;i++)
        if(!ctx->orphans[i].used){slot=i;break;}
    if(slot==NAS_LWEXT4_ORPHAN_LIMIT || ctx->orphan_serial==UINT32_MAX)
        return -ENOSPC;
    uint32_t serial=ctx->orphan_serial+1;
    char dest[256];
    int rc=ext_orphan_path(ctx,ino,serial,is_dir,dest);
    if(rc)return rc;
    struct ext4_inode raw;
    uint32_t existing=0;
    rc=ext4_raw_inode_fill(dest,&existing,&raw);
    if(!rc)return -EEXIST;
    if(rc!=ENOENT)return -rc;
    rc=ext_move_staged(from,dest,is_dir);
    if(rc)return rc;
    ctx->orphan_serial=serial;
    ctx->orphans[slot]=(struct nas_lwext4_orphan){
        .ino=ino,.serial=serial,.used=true,.is_dir=is_dir
    };
    return 0;
}
static void ext_last_inode_unpin(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->mount->private_data)return;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || node->ino>UINT32_MAX)return;
    for(unsigned i=0;i<NAS_LWEXT4_ORPHAN_LIMIT;i++) {
        struct nas_lwext4_orphan *orphan=&ctx->orphans[i];
        if(!orphan->used || orphan->ino!=(uint32_t)node->ino)continue;
        /* Last close can remove a private directory entry without calling
         * a public VFS namespace mutation. It still invalidates cookies. */
        ext_namespace_changed(ctx);
        char path[256];
        if(!ext_orphan_path(ctx,orphan->ino,orphan->serial,
                            orphan->is_dir,path) &&
           !ext_remove_staged(path,orphan->is_dir))
            orphan->used=false;
        /* Failed deletion is durably named; next mount reaps it. */
        return;
    }
}
static int ext_live_getattr(const struct nas_fs_node *node,
                             struct nas_fs_node *out) {
    if(!node || !node->mount || !node->mount->private_data || !out)
        return -EINVAL;
    struct nas_lwext4_mount *ctx=node->mount->private_data;
    if(!ctx->mounted || !ctx->upstream_mount || !ctx->upstream_super ||
       !ctx->device.ext.fs)return -ENODEV;
    if(!node->ino || node->ino>UINT32_MAX)return -ESTALE;
    /* The mounted blockdev publishes the ACTUAL upstream ext4_fs through
     * its public ABI. Read the inode through that filesystem and its live
     * cache, without following any name that may have been renamed, linked,
     * replaced, or staged under the private orphan directory. Accessing the
     * vendor-private ext4_mountpoint layout would be incorrect here. */
    struct ext4_inode_ref ref={0};
    int rc=ext4_fs_get_inode_ref(ctx->device.ext.fs,(uint32_t)node->ino,&ref);
    if(rc)return -rc;
    struct ext4_inode *raw=ref.inode;
    uint32_t mode=ext4_inode_get_mode(ctx->upstream_super,raw);
    out->mode=mode;
    out->nlink=ext4_inode_get_links_cnt(raw);
    out->uid=ext4_inode_get_uid(raw);
    out->gid=ext4_inode_get_gid(raw);
    out->atime_sec=ext4_inode_get_access_time(raw);
    out->mtime_sec=ext4_inode_get_modif_time(raw);
    out->ctime_sec=ext4_inode_get_change_inode_time(raw);
    out->size=ext4_inode_get_size(ctx->upstream_super,raw);
    out->is_dir=(mode&0170000U)==0040000U;
    out->is_symlink=(mode&0170000U)==0120000U;
    rc=ext4_fs_put_inode_ref(&ref);
    if(rc)return -rc;
    if(out->is_dir!=node->is_dir || out->is_symlink!=node->is_symlink)
        return -ESTALE;
    /* The on-disk hidden recovery name keeps i_links_count nonzero while
     * an unlinked inode is pinned. POSIX fstat must report logical zero. */
    for(unsigned i=0;i<NAS_LWEXT4_ORPHAN_LIMIT;i++)
        if(ctx->orphans[i].used && ctx->orphans[i].ino==node->ino) {
            out->nlink=0;
            break;
        }
    return 0;
}
static int ext_unlink_path(const struct nas_fs_node *parent,const char *name,uint64_t flags) {
    char path[256];
    int rc=ext_child_path(parent,name,path);
    if(rc)return rc;
    if(flags&~0x200ULL)return -EINVAL; /* AT_REMOVEDIR */
    ext_namespace_changed(parent->mount->private_data);
    if(!(flags&0x200ULL)) {
        struct nas_lwext4_mount *ctx=parent->mount->private_data;
        if(!ctx || !ctx->mounted)return -ENODEV;
        struct ext4_inode raw;
        uint32_t ino=0;
        rc=ext4_raw_inode_fill(path,&ino,&raw);
        if(rc)return -rc;
        if(!ino)return -EIO;
        uint32_t type=ext4_inode_get_mode(ctx->upstream_super,&raw)&0170000U;
        if(type==0040000U)return -EISDIR;
        struct nas_fs_node target={.mount=parent->mount,
            .filesystem=parent->filesystem,.mount_id=parent->mount_id,
            .inode=(const void *)(uintptr_t)ino,.ino=ino};
        if(ext4_inode_get_links_cnt(&raw)==1 && nas_vfs_node_has_lease(&target))
            return ext_stage_orphan(ctx,path,ino,false);
        return -ext4_fremove(path);
    }
    /* Rmdir of a nonempty directory is forbidden even if an owner pins
     * it. An EMPTY opened directory retains a hidden private link until
     * the final VFS unpin, never a recursive ext4_dir_rm. */
    rc=ext_dir_empty(path);
    if(rc)return rc;
    struct nas_lwext4_mount *ctx=parent->mount->private_data;
    if(!ctx || !ctx->mounted)return -ENODEV;
    uint32_t number=0;
    struct ext4_inode raw;
    rc=ext4_raw_inode_fill(path,&number,&raw);
    if(rc)return -rc;
    if(!number)return -EIO;
    if((ext4_inode_get_mode(ctx->upstream_super,&raw)&0170000U)!=0040000U)
        return -ENOTDIR;
    struct nas_fs_node target={.mount=parent->mount,
        .filesystem=parent->filesystem,.mount_id=parent->mount_id,
        .inode=(const void *)(uintptr_t)number,.ino=number};
    return nas_vfs_node_has_lease(&target) ?
           ext_stage_orphan(ctx,path,number,true) : -ext4_dir_rm(path);
}
/* Upstream ext4_frename is NO-REPLACE: ext4_create_hardlink returns
 * EEXIST for an existing destination. Replacement is adapted here, not
 * worked around in user BusyBox or by changing the vendor. This VFS is
 * currently single-CPU; a staged victim can be rolled back if source
 * rename fails, and a pinned LAST-link victim remains a live orphan. */
static int ext_rename_path(const struct nas_fs_node *old_parent,const char *old_name,
                            const struct nas_fs_node *new_parent,const char *new_name,
                            uint64_t flags) {
    if(flags&~1ULL)return -EINVAL; /* Linux RENAME_NOREPLACE only */
    if(!old_parent || !new_parent || old_parent->mount!=new_parent->mount)
        return -EXDEV;
    char from[256],to[256];
    int rc=ext_child_path(old_parent,old_name,from);
    if(rc)return rc;
    rc=ext_child_path(new_parent,new_name,to);
    if(rc)return rc;
    if(!strcmp(from,to))return 0;
    struct nas_lwext4_mount *ctx=old_parent->mount->private_data;
    if(!ctx || !ctx->mounted || !ctx->upstream_super)return -ENODEV;
    ext_namespace_changed(ctx);
    struct ext4_inode source_raw,victim_raw;
    uint32_t source=0,victim=0;
    rc=ext4_raw_inode_fill(from,&source,&source_raw);
    if(rc)return -rc;
    if(!source)return -EIO;
    uint32_t source_type=ext4_inode_get_mode(ctx->upstream_super,
                                             &source_raw)&0170000U;
    rc=ext4_raw_inode_fill(to,&victim,&victim_raw);
    if(rc==ENOENT)
        return ext_move_staged(from,to,source_type==0040000U);
    if(rc)return -rc;
    if(!victim)return -EIO;
    if(flags&1ULL)return -EEXIST;
    if(source==victim)return 0; /* POSIX identical hardlink inode */
    uint32_t victim_type=ext4_inode_get_mode(ctx->upstream_super,
                                             &victim_raw)&0170000U;
    if(source_type==0040000U && victim_type!=0040000U)return -ENOTDIR;
    if(source_type!=0040000U && victim_type==0040000U)return -EISDIR;
    const bool is_dir=source_type==0040000U;
    if(is_dir) {
        /* Linux rename may replace an EMPTY directory, including when an
         * open directory File still owns the victim inode. Upstream
         * ext4_dir_rm is recursive; it is safe only after this check. */
        rc=ext_dir_empty(to);
        if(rc)return rc;
    }
    bool defer=is_dir ||
               ext4_inode_get_links_cnt(&victim_raw)==1;
    if(defer) {
        struct nas_fs_node node={.mount=new_parent->mount,
            .filesystem=new_parent->filesystem,.mount_id=new_parent->mount_id,
            .ino=victim,.inode=(const void *)(uintptr_t)victim};
        defer=nas_vfs_node_has_lease(&node);
    }
    rc=ext_stage_orphan(ctx,to,victim,is_dir);
    if(rc)return rc;
    uint32_t serial=ctx->orphan_serial;
    char hidden[256];
    rc=ext_orphan_path(ctx,victim,serial,is_dir,hidden);
    if(rc)return rc; /* already preflighted by ext_stage_orphan */
    rc=ext_move_staged(from,to,is_dir);
    if(rc) {
        /* Keep old destination data safe even if source rename fails.
         * A failed rollback is a real EIO, not a silent successful rename. */
        int undo=ext_move_staged(hidden,to,is_dir);
        if(!undo) {
            for(unsigned i=0;i<NAS_LWEXT4_ORPHAN_LIMIT;i++)
                if(ctx->orphans[i].used && ctx->orphans[i].ino==victim &&
                   ctx->orphans[i].serial==serial)
                    ctx->orphans[i].used=false;
        }
        return undo ? -EIO : -rc;
    }
    if(!defer) {
        rc=ext_remove_staged(hidden,is_dir);
        if(!rc) {
            for(unsigned i=0;i<NAS_LWEXT4_ORPHAN_LIMIT;i++)
                if(ctx->orphans[i].used && ctx->orphans[i].ino==victim &&
                   ctx->orphans[i].serial==serial)
                    ctx->orphans[i].used=false;
        }
        /* If reclaim failed, rename is already committed: retain the
         * private orphan for next mount-time recovery, not a false errno. */
    }
    return 0;
}
static int ext_link_path(const struct nas_fs_node *old_parent,const char *old_name,
                         const struct nas_fs_node *new_parent,const char *new_name) {
    if(!old_parent || !new_parent || old_parent->mount!=new_parent->mount)
        return -EXDEV;
    char from[256],to[256];
    int rc=ext_child_path(old_parent,old_name,from);
    if(rc)return rc;
    rc=ext_child_path(new_parent,new_name,to);
    if(rc)return rc;
    ext_namespace_changed(old_parent->mount->private_data);
    return -ext4_flink(from,to);
}
static int ext_symlink_path(const struct nas_fs_node *parent,const char *name,
                            const char *target) {
    char path[256];int rc=ext_child_path(parent,name,path);
    if(rc)return rc;
    ext_namespace_changed(parent->mount->private_data);
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
                              uint64_t cursor,
                              struct nas_vfs_dirent *out,unsigned maximum) {
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
    /* Preserve the public logical cursor ABI. The vendor's byte offset is
     * a disposable mount-local hint for sequential batches, not VFS state. */
    const struct nas_lwext4_dir_hint previous=ctx->directory_hint;
    uint64_t scanned=0;
    if(previous.epoch && previous.epoch==ctx->namespace_epoch &&
       previous.ino==node->ino && previous.cursor<=cursor) {
        dir.next_off=previous.next_off;
        scanned=previous.cursor;
    }
    int error=0;
    unsigned copied=0;
    const ext4_direntry *entry;
    while((entry=ext4_dir_entry_next(&dir))) {
        if(!entry->inode)continue;
        if(node->ino==ctx->root_ino &&
           entry->name_length==sizeof(EXT4_ORPHAN_DIRECTORY)-1 &&
           !memcmp(entry->name,EXT4_ORPHAN_DIRECTORY,
                   sizeof(EXT4_ORPHAN_DIRECTORY)-1))continue;
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
    ctx->directory_hint=(struct nas_lwext4_dir_hint){
        .epoch=ctx->namespace_epoch,.ino=(uint32_t)node->ino,
        .cursor=scanned,.next_off=dir.next_off
    };
    return (int)copied;
}
static const struct nas_vfs_type ext_type={
    .name="lwext4",
    .exec_source_label="lwext4",
    .root=ext_lookup_root,.lookup_child=ext_lookup_child,
    .readlink=ext_readlink_node,
    .last_inode_unpin=ext_last_inode_unpin,
    .getattr=ext_live_getattr,
    .read_at=ext_read_inode,
    .read_size_is_eof=true,
    .write_at=ext_write_inode,
    .resize=ext_resize_inode,.sync=ext_sync_inode,
    .read_dir=ext_read_directory,
    .create=ext_create_path,.mkdir=ext_mkdir_path,
    .unlink=ext_unlink_path,.rename=ext_rename_path,
    .link=ext_link_path,.symlink=ext_symlink_path,
    .unlink_open_file_safe=true,
    .unlink_open_dir_safe=true,
    .rename_replace_open_safe=true,
    /* Capability is mount-specific; default guest mount remains RO. */
    .read_only=false
};
/* After unclean shutdown the private directory holds old open files.
 * Only delete recognizably named regular orphan records. Always restart
 * iteration after mutation; corrupt or foreign entries fail closed. */
static int ext_orphan_prepare(struct nas_lwext4_mount *ctx) {
    int rc=nas_vfs_join_child(ctx->mount_point,EXT4_ORPHAN_DIRECTORY,
                              ctx->orphan_dir);
    if(rc)return rc;
    char sample[256];
    rc=ext_orphan_path(ctx,UINT32_MAX,UINT32_MAX,false,sample);
    if(rc)return rc;
    rc=ext4_dir_mk(ctx->orphan_dir);
    if(rc)return -rc;
    for(unsigned round=0;round<65536;round++) {
        ext4_dir dir={0};
        rc=ext4_dir_open(&dir,ctx->orphan_dir);
        if(rc)return -rc;
        bool found=false,invalid=false;
        char name[19]={0};
        const ext4_direntry *item;
        while((item=ext4_dir_entry_next(&dir))) {
            if(!item->inode)continue;
            if(item->name_length==1 && item->name[0]=='.')continue;
            if(item->name_length==2 && item->name[0]=='.' &&
               item->name[1]=='.')continue;
            if(!ext_orphan_record(item->name,item->name_length) ||
               ((item->name[0]=='d') !=
                (item->inode_type==EXT4_DE_DIR))) {
                invalid=true;break;
            }
            /* The private filename must encode the inode actually
             * referenced by ext4's directory entry, not just resemble
             * an arbitrary orphan name inside a possibly preexisting
             * on-disk directory. Reject foreign/corrupt names. */
            uint32_t named_ino=0;
            for(unsigned i=1;i<=8;i++) {
                uint8_t digit=item->name[i];
                named_ino=(named_ino<<4)|
                    (digit<='9' ? (uint32_t)(digit-'0') :
                                  (uint32_t)(digit-'a'+10));
            }
            if(named_ino!=item->inode) {invalid=true;break;}
            memcpy(name,item->name,18);name[18]=0;
            found=true;break;
        }
        int close_rc=ext4_dir_close(&dir);
        if(close_rc)return -close_rc;
        if(invalid)return -EIO;
        if(!found)return 0;
        char path[256];
        rc=nas_vfs_join_child(ctx->orphan_dir,name,path);
        if(rc)return rc;
        rc=ext_remove_staged(path,name[0]=='d');
        if(rc)return rc;
    }
    return -EOVERFLOW;
}
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
    ctx->directory_hint=(struct nas_lwext4_dir_hint){0};
    ctx->namespace_epoch=1;
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
    struct ext4_inode root_raw;
    rc=ext4_raw_inode_fill(ctx->upstream_prefix,&ctx->root_ino,
                           &root_raw);
    if(rc){rc=-rc;goto FailMount;}
    if(!ctx->root_ino){rc=-EIO;goto FailMount;}
    if(writable) {
        rc=ext_orphan_prepare(ctx);
        if(rc)goto FailMount;
        rc=ext4_cache_flush(ctx->upstream_prefix);
        if(rc){rc=-rc;goto FailMount;}
        rc=nas_block_flush(ctx->device.storage);
        if(rc)goto FailMount;
    }
    ctx->mount=(struct nas_vfs_mount){
        .mountpoint=ctx->mount_point,
        .type=&ext_type,
        .allow_missing_mountpoint=true, /* internal early-boot /mnt */
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
    /* Do not discard a private inode still owned by a File; retry failed
     * final-close reclamation here before the physical filesystem detach. */
    for(unsigned i=0;i<NAS_LWEXT4_ORPHAN_LIMIT;i++) {
        struct nas_lwext4_orphan *o=&ctx->orphans[i];
        if(!o->used)continue;
        struct nas_fs_node mounted_root;
        char resolved[256];
        int rc=nas_fs_lookup_node(ctx->mount_point,true,
                                   &mounted_root,resolved);
        if(rc)return rc;
        struct nas_fs_node target={.mount=mounted_root.mount,
            .filesystem=mounted_root.filesystem,
            .mount_id=mounted_root.mount_id,.inode=(void *)(uintptr_t)o->ino,
            .ino=o->ino};
        if(nas_vfs_node_has_lease(&target))return -EBUSY;
        char path[256];
        rc=ext_orphan_path(ctx,o->ino,o->serial,o->is_dir,path);
        if(rc)return rc;
        struct ext4_inode raw;uint32_t live=0;
        rc=ext4_raw_inode_fill(path,&live,&raw);
        if(rc!=ENOENT && rc)return -rc;
        if(!rc && live!=o->ino)return -ESTALE;
        if(!rc && (rc=ext_remove_staged(path,o->is_dir)))return rc;
        o->used=false;
    }
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
