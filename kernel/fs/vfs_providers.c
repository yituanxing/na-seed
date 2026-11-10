#include "vfs.h"
#include "lookup.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "procfs.h"
#include "devfs.h"
#include "../errno.h"
#include "../mm/frame.h"
#include "../block/device.h"
#include "../lib/string.h"
#include <stddef.h>

/* Cursor contract matches every mounted provider: '.', '..', children. */
static int fill_dirent(struct nas_vfs_dirent *out,const char *name,
                       uint64_t ino,unsigned dtype) {
    if(!out || !name || !ino)return -EIO;
    size_t len=strlen(name);
    if(!len || len>255)return -ENAMETOOLONG;
    *out=(struct nas_vfs_dirent){.ino=ino,.dtype=(uint8_t)dtype,
                                 .name_len=(uint8_t)len};
    memcpy(out->name,name,len+1);
    return 0;
}
static void get_parent(const char *path,char parent[256]) {
    size_t n=strlen(path);
    memcpy(parent,path,n+1);
    while(n>1 && parent[n-1]!='/')--n;
    if(n>1)--n;
    parent[n]=0;
}
static unsigned file_dtype(uint32_t mode) {
    switch(mode&0170000U) {
    case 0040000U:return 4;
    case 0120000U:return 10;
    case 0020000U:return 2;
    case 0060000U:return 6;
    case 0010000U:return 1;
    case 0140000U:return 12;
    default:return 8;
    }
}
static const char *basename(const char *path) {
    const char *last=strrchr(path,'/');
    return last?last+1:path;
}

static int boot_fill(const struct bootfs_entry *e,struct nas_fs_node *out) {
    if(!e)return -ENOENT;
    *out=(struct nas_fs_node){.inode=e,.data=e->data,.size=e->size,
      .ino=e->ino,.nlink=bootfs_is_dir(e)?2:(e->nlink?e->nlink:1),
      .mtime_sec=e->mtime_sec,.atime_sec=e->mtime_sec,
      .ctime_sec=e->mtime_sec,
      .mode=e->mode,.is_dir=bootfs_is_dir(e),.is_symlink=bootfs_is_link(e)};
    return 0;
}
static int boot_root(const struct nas_vfs_mount *mount,
                     struct nas_fs_node *out) {
    (void)mount;
    return boot_fill(bootfs_find("/"),out);
}
static int boot_child(const struct nas_fs_node *parent,const char *name,
                      struct nas_fs_node *out) {
    if(!parent || !parent->inode || !parent->is_dir)return -ENOTDIR;
    if(!name || !name[0] || strchr(name,'/'))return -EINVAL;
    /* Archive path indexing is an internal bootfs detail. Derive it from
     * the directory's OWN immutable inode, never the VFS mountpoint.
     * Keep O(1) expected lookup via bootfs_find's existing hash table,
     * rather than rescanning all archive children for each path step. */
    const struct bootfs_entry *dir=parent->inode;
    char local[256];
    int rc=nas_vfs_join_child(dir->path,name,local);
    return rc ? rc : boot_fill(bootfs_find(local),out);
}
static int boot_readlink(const struct nas_fs_node *node,char target[256]) {
    const struct bootfs_entry *e=node->inode;
    if(!e || !e->data || e->size>=256)return -ENAMETOOLONG;
    memcpy(target,e->data,(size_t)e->size);target[e->size]=0;
    return (int)e->size;
}
static int64_t boot_read(const struct nas_fs_node *node,uint64_t offset,
                         uint8_t *dst,uint64_t length) {
    if(offset>=node->size)return 0;
    if(length>node->size-offset)length=node->size-offset;
    if(!node->data)return -EIO;
    memcpy(dst,node->data+offset,(size_t)length);
    return (int64_t)length;
}

static int boot_read_dir(const struct nas_fs_node *node,
                         uint64_t cursor,struct nas_vfs_dirent *out,
                         unsigned maximum) {
    if(!node || !node->is_dir || !node->inode || !out || !maximum)
        return -EINVAL;
    const struct bootfs_entry *dir=node->inode;
    unsigned count=0;
    for(;count<maximum;count++,cursor++) {
        const char *name;
        uint64_t ino;
        unsigned dtype=4;
        if(cursor==0) {name=".";ino=dir->ino;}
        else if(cursor==1) {
            char parent[256];
            get_parent(dir->path,parent);
            const struct bootfs_entry *p=bootfs_find(parent);
            ino=p?p->ino:dir->ino;name="..";
        } else {
            if(cursor-2>UINT32_MAX)break;
            const struct bootfs_entry *entry=bootfs_child(dir,(unsigned)(cursor-2));
            if(!entry)break;
            ino=entry->ino;
            dtype=file_dtype(entry->mode);
            name=basename(entry->path);
        }
        int rc=fill_dirent(&out[count],name,ino,dtype);
        if(rc)return rc;
    }
    return (int)count;
}

static const struct nas_vfs_type boot_type={
    .name="bootfs",.exec_source_label="CPIO",
    .root=boot_root,.lookup_child=boot_child,
    .readlink=boot_readlink,
    .read_at=boot_read,.read_size_is_eof=true,
    .read_dir=boot_read_dir,.read_only=true,.immutable_file_data=true
};
static void tmp_fill(const struct nas_tmp_file *f,
                     struct nas_fs_node *out) {
    *out=(struct nas_fs_node){.inode=f,
      .size=f->is_dir ? tmp_directory_stat_size(f) : f->size,
      .ino=5000+tmp_number(f),.nlink=f->nlink?(f->is_dir?2:f->nlink):0,
      .atime_sec=f->atime_ns/1000000000ULL,
      .mtime_sec=f->mtime_ns/1000000000ULL,
      .ctime_sec=f->ctime_ns/1000000000ULL,
      .atime_nsec=(uint32_t)(f->atime_ns%1000000000ULL),
      .mtime_nsec=(uint32_t)(f->mtime_ns%1000000000ULL),
      .ctime_nsec=(uint32_t)(f->ctime_ns%1000000000ULL),
      .mode=f->mode,.uid=f->uid,.gid=f->gid,
      .is_dir=f->is_dir,.is_symlink=tmp_is_link(f)};
}
/* The /tmp mountpoint uses an immutable bootfs identity, but its directory
 * entries and timestamps belong to the live tmpfs namespace.  Snapshot the
 * provider-owned root clock rather than publishing the bootfs archive time. */
static void tmp_fill_root_times(struct nas_fs_node *out) {
    uint64_t mtime=0,ctime=0;
    tmp_root_times(&mtime,&ctime);
    out->mtime_sec=mtime/1000000000ULL;
    out->mtime_nsec=(uint32_t)(mtime%1000000000ULL);
    out->ctime_sec=ctime/1000000000ULL;
    out->ctime_nsec=(uint32_t)(ctime%1000000000ULL);
    out->size=tmp_root_directory_stat_size();
}
static bool tmp_is_overlay_root(const struct nas_fs_node *node) {
    const struct bootfs_entry *root=bootfs_find("/tmp");
    return node && root && node->inode==(const void *)root;
}
static int tmp_root(const struct nas_vfs_mount *mount,
                    struct nas_fs_node *out) {
    if(mount && mount->private_data) {
        const struct nas_tmp_file *root=mount->private_data;
        if(!root->used || !root->is_dir || !root->is_mount_root)
            return -ESTALE;
        tmp_fill(root,out);
        return 0;
    }
    /* The bootstrap /tmp overlay root is backed by the immutable CPIO
     * directory; children resolve through tmpfs unless fallback is needed. */
    const struct bootfs_entry *root=bootfs_find("/tmp");
    if(!root || !bootfs_is_dir(root))return -ENOENT;
    *out=(struct nas_fs_node){.inode=root,.ino=root->ino,
            .nlink=2,.mode=root->mode,.is_dir=true};
    tmp_fill_root_times(out);
    return 0;
}
static int tmp_child(const struct nas_fs_node *parent,const char *name,
                     struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    struct nas_tmp_file *f=tmp_find_child(parent->inode,name);
    if(!f)return -ENOENT;
    tmp_fill(f,out);
    return 0;
}
static int tmp_readlink(const struct nas_fs_node *node,char target[256]) {
    const struct nas_tmp_file *f=node->inode;
    if(!f || f->size>=256)return -ENAMETOOLONG;
    int rc=tmp_link_target(f,target);
    return rc ? rc : (int)f->size;
}
static int64_t tmp_read(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *dst,uint64_t length) {
    return tmp_read_at(node->inode,offset,dst,length);
}
/* Namespace mutations use the stable parent inode and one basename
 * DIRECTLY, just like lookup_child. No provider callback reconstructs an
 * old absolute pathname. A directory rename moves one dentry edge. */
static int tmp_create_node(const struct nas_fs_node *parent,const char *name,
                           uint64_t mode) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    return tmp_create_at(parent->inode,name,false,(uint32_t)mode) ?
           0 : -ENOSPC;
}
/* memfd uses a real tmpfs inode with no name and no directory link.
 * VFS immediately owns a mount+inode pin, so the existing orphan reclaimer
 * runs on final File/VMA unpin without a second anonymous-file store. */
static int tmp_create_anonymous(const struct nas_vfs_mount *mount,
                                uint64_t mode,struct nas_fs_node *out) {
    if(!mount || !out || (mode&~07777ULL))return -EINVAL;
    struct nas_tmp_file *inode=tmp_inode_alloc(false,(uint32_t)mode);
    if(!inode)return -ENOSPC;
    tmp_fill(inode,out);
    return 0;
}
static void tmp_abort_anonymous(const struct nas_fs_node *node) {
    if(node && node->inode)
        tmp_inode_discard((struct nas_tmp_file *)(uintptr_t)node->inode);
}
static int tmp_mknod_node(const struct nas_fs_node *parent,const char *name,
                          uint64_t mode,struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    if(!out || !name)return -EINVAL;
    const uint64_t type=mode&0170000ULL;
    if(type!=0140000ULL && type!=0010000ULL)return -EOPNOTSUPP;
    if(tmp_find_child(parent->inode,name))return -EEXIST;
    /* The tmpfs namespace owner initializes S_IFSOCK or S_IFIFO before
     * publishing the dentry. Never expose a transient regular-file inode
     * as the old AF_UNIX bind path did. */
    struct nas_tmp_file *created=tmp_mknod_at(parent->inode,name,(uint32_t)mode);
    if(!created)return -ENOSPC;
    tmp_fill(created,out);
    return 0;
}
static int64_t tmp_write_node(const struct nas_fs_node *node,uint64_t off,
                              const uint8_t *src,uint64_t count) {
    return tmp_write_at((struct nas_tmp_file *)(uintptr_t)node->inode,
                        off,src,count);
}
static int tmp_shared_page(const struct nas_fs_node *node,uint64_t offset,
                            uint64_t *pa_owned) {
    return tmp_acquire_shared_page((struct nas_tmp_file *)(uintptr_t)node->inode,
                                   offset,pa_owned);
}
static int tmp_shared_resident(const struct nas_fs_node *node,uint64_t offset) {
    return tmp_page_resident(node->inode,offset);
}
static int tmp_shared_dirty(const struct nas_fs_node *node) {
    return tmp_touch_shared((struct nas_tmp_file *)(uintptr_t)node->inode);
}
static int tmp_resize_node(const struct nas_fs_node *node,uint64_t size) {
    return tmp_resize((struct nas_tmp_file *)(uintptr_t)node->inode,size);
}
static int tmp_sync_node(const struct nas_fs_node *node) {
    (void)node;return 0; /* Volatile memory is the final data store. */
}
static int tmp_chmod_node(const struct nas_fs_node *node,uint64_t mode) {
    if(tmp_is_overlay_root(node))return -EROFS;
    return tmp_chmod((struct nas_tmp_file *)(uintptr_t)node->inode,mode);
}
static int tmp_chown_node(const struct nas_fs_node *node,uint32_t uid,uint32_t gid) {
    /* /tmp mount root is an immutable CPIO directory, never a tmpfs slab. */
    if(tmp_is_overlay_root(node))return -EROFS;
    return tmp_chown((struct nas_tmp_file *)(uintptr_t)node->inode,uid,gid);
}
static int tmp_utimens_node(const struct nas_fs_node *node,uint64_t a,
                           uint64_t m,bool sa,bool sm) {
    if(tmp_is_overlay_root(node))return -EROFS;
    return tmp_utimens((struct nas_tmp_file *)(uintptr_t)node->inode,a,m,sa,sm);
}

/* A directory FD enumerates children via its owned inode, even after
 * multiple ancestor directory renames. Names are not path strings. */
static int tmp_read_dir(const struct nas_fs_node *node,
                        uint64_t cursor,struct nas_vfs_dirent *out,
                        unsigned maximum) {
    if(!node || !node->is_dir || !node->inode || !out || !maximum)return -EINVAL;
    const struct bootfs_entry *root=bootfs_find("/tmp");
    const bool is_root=root && node->inode==(const void *)root;
    if(!is_root) {
        const struct nas_tmp_file *directory=node->inode;
        if(!directory->used || !directory->is_dir)return -ENOTDIR;
        tmp_inode_access((struct nas_tmp_file *)(uintptr_t)directory);
    }
    const void *parent_inode=tmp_dir_parent_inode(node->inode);
    if(!parent_inode)return -ESTALE;
    unsigned count=0;
    for(;count<maximum;count++,cursor++) {
        const char *name;
        uint64_t ino=node->ino;
        unsigned dtype=4;
        if(cursor==0)name=".";
        else if(cursor==1) {
            ino=(root && parent_inode==(const void *)root) ?
                root->ino : 5000+tmp_number(parent_inode);
            name="..";
        } else {
            if(cursor-2>UINT32_MAX)break;
            const struct nas_tmp_dentry *entry=
                tmp_child_entry_inode(node->inode,(unsigned)(cursor-2));
            if(!entry)break;
            if(!entry->inode)return -EIO;
            ino=5000+tmp_number(entry->inode);
            dtype=file_dtype(entry->inode->mode);
            name=entry->name;
        }
        int rc=fill_dirent(&out[count],name,ino,dtype);
        if(rc)return rc;
    }
    return (int)count;
}
static int tmp_mkdir_node(const struct nas_fs_node *parent,const char *name,
                          uint64_t mode) {
    return parent && parent->is_dir ?
           tmp_mkdir_at(parent->inode,name,mode) : -ENOTDIR;
}
static int tmp_unlink_node(const struct nas_fs_node *parent,const char *name,
                           uint64_t flags) {
    return parent && parent->is_dir ?
           tmp_unlink_at(parent->inode,name,flags) : -ENOTDIR;
}
static int tmp_rename_node(const struct nas_fs_node *old_parent,const char *old_name,
                           const struct nas_fs_node *new_parent,const char *new_name,
                           uint64_t flags) {
    if(!old_parent || !old_parent->is_dir ||
       !new_parent || !new_parent->is_dir)return -ENOTDIR;
    return tmp_rename_at(old_parent->inode,old_name,new_parent->inode,new_name,flags);
}
static int tmp_link_node(const struct nas_fs_node *old_parent,const char *old_name,
                         const struct nas_fs_node *new_parent,const char *new_name) {
    if(!old_parent || !old_parent->is_dir ||
       !new_parent || !new_parent->is_dir)return -ENOTDIR;
    return tmp_link_at(old_parent->inode,old_name,new_parent->inode,new_name);
}
static int tmp_symlink_node(const struct nas_fs_node *parent,const char *name,
                            const char *target) {
    return parent && parent->is_dir ?
           tmp_symlink_at(parent->inode,name,target) : -ENOTDIR;
}
/* tmpfs inode slabs reuse an address after last unlink+unpin.
 * A monotonically allocated tmp inode number catches stale snapshots even
 * if the old lease was released before the caller attempts a read. */
static int tmp_validate_inode(const struct nas_fs_node *node) {
    if(!node || !node->inode)return -ESTALE;
    const struct bootfs_entry *root=bootfs_find("/tmp");
    if(root && node->inode==(const void *)root)
        return node->ino==root->ino && node->is_dir ? 0 : -ESTALE;
    const struct nas_tmp_file *file=node->inode;
    return file->used && node->ino==5000+tmp_number(file) &&
           node->is_dir==file->is_dir ? 0 : -ESTALE;
}
static int tmp_getattr(const struct nas_fs_node *node,struct nas_fs_node *out) {
    if(!node || !out || !node->inode)return -EINVAL;
    /* /tmp may expose a borrowed bootfs directory as its overlay root. */
    const struct bootfs_entry *root=bootfs_find("/tmp");
    if(root && node->inode==(const void *)root) {
        *out=*node;
        tmp_fill_root_times(out);
        return 0;
    }
    const struct nas_tmp_file *f=node->inode;
    if(!f->used || 5000+tmp_number(f)!=node->ino)return -ESTALE;
    *out=*node;
    out->mode=f->mode;
    out->uid=f->uid;
    out->gid=f->gid;
    out->size=f->is_dir ? tmp_directory_stat_size(f) : f->size;
    out->nlink=f->nlink ? (f->is_dir?2:f->nlink):0;
    out->atime_sec=f->atime_ns/1000000000ULL;
    out->mtime_sec=f->mtime_ns/1000000000ULL;
    out->ctime_sec=f->ctime_ns/1000000000ULL;
    out->atime_nsec=(uint32_t)(f->atime_ns%1000000000ULL);
    out->mtime_nsec=(uint32_t)(f->mtime_ns%1000000000ULL);
    out->ctime_nsec=(uint32_t)(f->ctime_ns%1000000000ULL);
    return 0;
}
static void tmp_last_inode_unpin(const struct nas_fs_node *node) {
    if(!node || !node->inode)return;
    /* Synthetic /tmp overlay root borrows a CPIO inode. Never pass that
     * foreign pointer to the tmpfs allocator/reclaimer. */
    const struct bootfs_entry *root=bootfs_find("/tmp");
    if(root && node->inode==(const void *)root)return;
    tmp_inode_discard((struct nas_tmp_file *)(uintptr_t)node->inode);
}
static int tmp_statfs(const struct nas_fs_node *node,
                      struct nas_vfs_statfs *out) {
    const struct nas_tmp_file *file=node ? node->inode : NULL;
    const struct nas_tmp_quota *quota=file ? file->quota : NULL;
    if(quota) {
        /* Mounted instances report their OWN accounted limits, rather
         * than advertising the whole shared frame pool as available. */
        uint64_t free=quota->max_pages-quota->used_pages;
        if(free>nas_free_frames())free=nas_free_frames();
        *out=(struct nas_vfs_statfs){
            .magic=0x01021994ULL,.block_size=4096,
            .blocks=quota->max_pages,.free_blocks=free,
            .available_blocks=free,.files=quota->max_inodes,
            .free_files=quota->max_inodes-quota->used_inodes,
            .fragment_size=4096,.name_max=255
        };
        return 0;
    }
    /* NaSeed tmpfs allocates sparse pages from the shared physical frame
     * pool, with no separate fixed tmpfs quota. These are the actual
     * available backing frames, not a fabricated constant capacity. */
    *out=(struct nas_vfs_statfs){
        .magic=0x01021994ULL, /* Linux TMPFS_MAGIC */
        .block_size=4096,.blocks=nas_managed_frames(),
        .free_blocks=nas_free_frames(),
        .available_blocks=nas_free_frames(),
        .fragment_size=4096,.name_max=255
    };
    return 0;
}
static int tmp_setxattr_node(const struct nas_fs_node *node,const char *name,
                             const uint8_t *value,uint64_t size,unsigned flags) {
    if(tmp_is_overlay_root(node))return -EOPNOTSUPP;
    return tmp_setxattr((struct nas_tmp_file *)(uintptr_t)node->inode,
                        name,value,size,flags);
}
static int64_t tmp_getxattr_node(const struct nas_fs_node *node,
                                 const char *name,uint8_t *value,uint64_t cap) {
    if(tmp_is_overlay_root(node))return -EOPNOTSUPP;
    return tmp_getxattr(node->inode,name,value,cap);
}
static int64_t tmp_listxattr_node(const struct nas_fs_node *node,
                                  uint8_t *names,uint64_t cap) {
    if(tmp_is_overlay_root(node))return 0;
    return tmp_listxattr(node->inode,names,cap);
}
static int tmp_removexattr_node(const struct nas_fs_node *node,
                                const char *name) {
    if(tmp_is_overlay_root(node))return -EOPNOTSUPP;
    return tmp_removexattr((struct nas_tmp_file *)(uintptr_t)node->inode,name);
}
static int tmp_fiemap_node(const struct nas_fs_node *node,uint64_t start,
                           uint64_t end,uint64_t *logical,uint64_t *length,
                           bool *last) {
    if(tmp_is_overlay_root(node))return -EISDIR;
    return tmp_fiemap_next(node->inode,start,end,logical,length,last);
}
static int tmp_preallocate_node(const struct nas_fs_node *node,
                                uint64_t offset,uint64_t length,unsigned mode) {
    if(!node || !node->inode)return -EINVAL;
    if(tmp_is_overlay_root(node))return -EISDIR;
    return tmp_preallocate((struct nas_tmp_file *)(uintptr_t)node->inode,
                           offset,length,mode);
}
static const struct nas_vfs_type tmp_type={
    .name="tmpfs",.exec_source_label="tmpfs",
    .root=tmp_root,.lookup_child=tmp_child,
    .last_inode_unpin=tmp_last_inode_unpin,.getattr=tmp_getattr,
    .statfs=tmp_statfs,
    .setxattr=tmp_setxattr_node,.getxattr=tmp_getxattr_node,
    .listxattr=tmp_listxattr_node,.removexattr=tmp_removexattr_node,
    .fiemap_next=tmp_fiemap_node,
    .validate_inode=tmp_validate_inode,
    .readlink=tmp_readlink,
    .read_at=tmp_read,.read_size_is_eof=true,
    .read_dir=tmp_read_dir,.chmod=tmp_chmod_node,.chown=tmp_chown_node,
    .utimens=tmp_utimens_node,
    .create=tmp_create_node,.create_anonymous=tmp_create_anonymous,
    .abort_anonymous=tmp_abort_anonymous,.mknod=tmp_mknod_node,
    .write_at=tmp_write_node,
    .acquire_shared_page=tmp_shared_page,
    .shared_page_resident=tmp_shared_resident,.touch_shared=tmp_shared_dirty,
    .resize=tmp_resize_node,.preallocate=tmp_preallocate_node,
    .sync=tmp_sync_node,
    .mkdir=tmp_mkdir_node,.unlink=tmp_unlink_node,.rename=tmp_rename_node,
    .link=tmp_link_node,.symlink=tmp_symlink_node,
    .unlink_open_safe=true,.rename_replace_open_safe=true
};
int nas_vfs_create_anonymous_tmpfs(uint64_t mode,struct nas_fs_node *owned) {
    return nas_vfs_create_anonymous(&tmp_type,mode,owned);
}

/* Each runtime tmpfs attachment owns a fresh superblock/root, unlike the
 * historical bootfs-backed /tmp overlay. Static registration tokens have
 * stable identity through VFS attach/detach (no slot compaction). */
#define NAS_RUNTIME_TMPFS_MAX 8U
static struct runtime_tmpfs {
    struct nas_vfs_mount mount;
    struct nas_tmp_quota quota;
    char target[256];
    char source[64];
    struct nas_tmp_file *root;
    bool used;
} runtime_tmpfs[NAS_RUNTIME_TMPFS_MAX];

int nas_vfs_mount_tmpfs(const char *target,const char *source,
                        uint32_t mode,bool read_only,uint64_t max_pages,
                        uint64_t max_inodes,bool strict_atime) {
    if(!target || !source || !source[0] ||
       strlen(target)>=sizeof(runtime_tmpfs[0].target) ||
       strlen(source)>=sizeof(runtime_tmpfs[0].source) ||
       (mode&~07777U) || !max_pages || !max_inodes)return -EINVAL;
    struct runtime_tmpfs *entry=NULL;
    for(unsigned i=0;i<NAS_RUNTIME_TMPFS_MAX;i++)
        if(!runtime_tmpfs[i].used) {entry=&runtime_tmpfs[i];break;}
    if(!entry)return -ENOSPC;
    entry->quota=(struct nas_tmp_quota){
        .max_pages=max_pages,.max_inodes=max_inodes,
        .strict_atime=strict_atime
    };
    struct nas_tmp_file *root=tmp_inode_alloc_quota(true,mode,&entry->quota);
    if(!root)return -ENOMEM;
    root->is_mount_root=true;
    tmp_inode_link(root); /* superblock's reference until detach */
    memcpy(entry->target,target,strlen(target)+1);
    memcpy(entry->source,source,strlen(source)+1);
    entry->root=root;
    entry->mount=(struct nas_vfs_mount){
        .mountpoint=entry->target,.type=&tmp_type,
        .private_data=root,.source_label=entry->source,
        .read_only=read_only
    };
    int rc=nas_vfs_register_mount(&entry->mount);
    if(rc) {
        root->is_mount_root=false;
        tmp_inode_unlink(root);
        entry->root=NULL;
        return rc;
    }
    entry->used=true;
    return 0;
}

int nas_vfs_umount_tmpfs(const char *target) {
    if(!target)return -EINVAL;
    for(unsigned i=0;i<NAS_RUNTIME_TMPFS_MAX;i++) {
        struct runtime_tmpfs *entry=&runtime_tmpfs[i];
        if(!entry->used || strcmp(entry->target,target))continue;
        int rc=nas_vfs_unregister_mount(&entry->mount);
        if(rc)return rc;
        rc=tmp_destroy_mount_root(entry->root);
        if(rc)return rc; /* This would violate a VFS ownership invariant. */
        *entry=(struct runtime_tmpfs){0};
        return 0;
    }
    return -EINVAL;
}

/* A real Alpine OpenRC boot can create runtime tmpfs mounts (notably /run)
 * that outlive the shell and even PID1. The usual syscall unmount is not
 * automatically invoked on process exit. For final physical ext4 shutdown,
 * once all Tasks and their FS/OpenFile references are quiesced, replay that
 * same provider-owned unmount in deepest-first order. Otherwise its counted
 * parent/covered Dentry anchors keep ext4's root child_refs and open_refs
 * nonzero and upstream ext4_umount correctly refuses EBUSY. */
int nas_vfs_shutdown_runtime_tmpfs(void) {
    for(unsigned remaining=0;remaining<NAS_RUNTIME_TMPFS_MAX;remaining++) {
        struct runtime_tmpfs *selected=NULL;
        size_t deepest=0;
        for(unsigned i=0;i<NAS_RUNTIME_TMPFS_MAX;i++) {
            struct runtime_tmpfs *entry=&runtime_tmpfs[i];
            if(!entry->used)continue;
            size_t depth=strlen(entry->target);
            if(!selected || depth>deepest) {
                selected=entry;
                deepest=depth;
            }
        }
        if(!selected)return 0;
        /* umount owns the VFS registration and actual tmpfs inode tree;
         * do not clear a slot or decrement parent pins behind its back. */
        int rc=nas_vfs_umount_tmpfs(selected->target);
        if(rc)return rc;
    }
    for(unsigned i=0;i<NAS_RUNTIME_TMPFS_MAX;i++)
        if(runtime_tmpfs[i].used)return -EBUSY;
    return 0;
}
static void proc_fill(struct nas_proc_node p,struct nas_fs_node *out) {
    *out=(struct nas_fs_node){.ino=nas_proc_ino(p),
      .nlink=nas_proc_is_dir(p)?2:1,.mode=nas_proc_mode(p),
      .is_dir=nas_proc_is_dir(p),
      /* procfd target text is intentionally not namei-followable yet. */
      .is_symlink=p.kind==NAS_PROC_PID_CWD_LINK};
}
static int proc_root(const struct nas_vfs_mount *mount,
                     struct nas_fs_node *out) {
    (void)mount;
    struct nas_proc_node p;
    if(!nas_proc_lookup("/proc",&p))return -ENOENT;
    proc_fill(p,out);
    return 0;
}
static int proc_child(const struct nas_fs_node *parent,const char *name,
                      struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    struct nas_proc_node p;
    if(!nas_proc_find_child(parent->ino,name,&p))return -ENOENT;
    proc_fill(p,out);
    return 0;
}

/* /proc/<pid>/fd/<n> text is a dynamic link supplied by the process FD
 * owner. Do not mark it namei-followable until open-through-fd is supported. */
static int proc_readlink_node(const struct nas_fs_node *node,
                              char target[256]) {
    if(!node || !target)return -EINVAL;
    /* /proc/self is only a spelling. Resolve once to a stable PID/FD
     * inode: an inherited handle must never retarget to the child PID. */
    struct nas_proc_node proc;
    if(!nas_proc_fd_link_from_ino(node->ino,&proc) &&
       (!nas_proc_node_from_ino(node->ino,&proc) ||
        proc.kind!=NAS_PROC_PID_CWD_LINK))return -ENOENT;
    int64_t n=nas_proc_readlink(proc,target,256);
    if(n<0)return (int)n;
    return n>256 ? -ENAMETOOLONG : (int)n;
}
static int proc_read_dir(const struct nas_fs_node *node,
                         uint64_t cursor,struct nas_vfs_dirent *out,
                         unsigned maximum) {
    if(!node || !node->is_dir || !out || !maximum)return -EINVAL;
    struct nas_proc_node directory;
    /* Dynamic /proc/self is resolved once at open. An inherited File
     * must enumerate its original process directory after fork rather
     * than reinterpreting 'self' in the current task. */
    if(!nas_proc_directory_from_ino(node->ino,&directory))return -ESTALE;
    unsigned count=0;
    for(;count<maximum;count++,cursor++) {
        char name[32];
        struct nas_proc_node entry=directory;
        unsigned dtype=4;
        if(cursor==0) {name[0]='.';name[1]=0;}
        else if(cursor==1) {name[0]='.';name[1]='.';name[2]=0;}
        else {
            if(cursor-2>UINT32_MAX ||
               !nas_proc_child(directory,(unsigned)(cursor-2),name,&entry))
                break;
            dtype=nas_proc_is_dir(entry)?4:
                  entry.kind==NAS_PROC_FD_LINK ||
                  entry.kind==NAS_PROC_PID_CWD_LINK?10:8;
        }
        int rc=fill_dirent(&out[count],name,nas_proc_ino(entry),dtype);
        if(rc)return rc;
    }
    return (int)count;
}

static int64_t proc_read_inode(const struct nas_fs_node *node,uint64_t offset,
                                uint8_t *dst,uint64_t length) {
    if(!node || !node->mount || (!dst && length))return -EINVAL;
    struct nas_proc_node proc;
    if(!nas_proc_node_from_ino(node->ino,&proc))return -ESTALE;
    if(nas_proc_is_dir(proc))return -EISDIR;
    if(length>UINT32_MAX)length=UINT32_MAX;
    return nas_proc_read(proc,offset,dst,(unsigned)length);
}
static const struct nas_vfs_type proc_type={
    .name="procfs",.exec_source_label="procfs",
    .root=proc_root,.lookup_child=proc_child,
    .readlink=proc_readlink_node,.read_at=proc_read_inode,
    .read_dir=proc_read_dir,.read_only=true
};

/* The current QEMU targets boot one logical CPU (-smp 1). Expose that
 * topology through an actual read-only VFS provider, not app-specific files
 * staged in a test image. The table is one inode/parent graph for lookup,
 * readdir, open and reads; absent CPU capabilities remain absent. */
enum {
    SYS_ROOT, SYS_DEVICES, SYS_SYSTEM, SYS_CPU, SYS_CPU0, SYS_TOPOLOGY,
    SYS_POSSIBLE, SYS_PRESENT, SYS_ONLINE, SYS_OFFLINE, SYS_KERNEL_MAX,
    SYS_CORE_ID, SYS_PACKAGE_ID, SYS_THREAD_SIBLINGS, SYS_CORE_SIBLINGS,
    SYS_MEMORY, SYS_MEMORY_BLOCK_SIZE,
    SYS_DEV, SYS_DEV_BLOCK, SYS_BLOCK, SYS_VIRTUAL, SYS_VIRTUAL_BLOCK,
    SYS_NODE_COUNT
};
#define SYS_INO_BASE 900000ULL
/* Every memory block owns a stable directory inode and two read-only
 * attributes; its number is the physical 128 MiB block, not a dense index.
 * Keep this range disjoint from the fixed CPU-topology inode table. */
#define SYS_MEMORY_INO_BASE (1ULL << 48)
#define SYS_MEMORY_INO_STRIDE 3ULL
_Static_assert(NAS_MEMORY_BLOCK_BYTES==0x8000000ULL,
               "sysfs block_size_bytes must match physical block granularity");
enum { SYS_MEMORY_DIR, SYS_MEMORY_STATE, SYS_MEMORY_REMOVABLE };
struct sys_entry {
    unsigned parent;
    const char *name;
    const char *contents;
    bool directory;
};
static const struct sys_entry sys_entries[SYS_NODE_COUNT]={
    [SYS_ROOT]={SYS_ROOT,"",0,true},
    [SYS_DEVICES]={SYS_ROOT,"devices",0,true},
    [SYS_SYSTEM]={SYS_DEVICES,"system",0,true},
    [SYS_CPU]={SYS_SYSTEM,"cpu",0,true},
    [SYS_CPU0]={SYS_CPU,"cpu0",0,true},
    [SYS_TOPOLOGY]={SYS_CPU0,"topology",0,true},
    [SYS_POSSIBLE]={SYS_CPU,"possible","0\n",false},
    [SYS_PRESENT]={SYS_CPU,"present","0\n",false},
    [SYS_ONLINE]={SYS_CPU,"online","0\n",false},
    [SYS_OFFLINE]={SYS_CPU,"offline","\n",false},
    [SYS_KERNEL_MAX]={SYS_CPU,"kernel_max","0\n",false},
    [SYS_CORE_ID]={SYS_TOPOLOGY,"core_id","0\n",false},
    [SYS_PACKAGE_ID]={SYS_TOPOLOGY,"physical_package_id","0\n",false},
    [SYS_THREAD_SIBLINGS]={SYS_TOPOLOGY,"thread_siblings_list","0\n",false},
    [SYS_CORE_SIBLINGS]={SYS_TOPOLOGY,"core_siblings_list","0\n",false},
    [SYS_MEMORY]={SYS_SYSTEM,"memory",0,true},
    [SYS_MEMORY_BLOCK_SIZE]={SYS_MEMORY,"block_size_bytes","8000000\n",false},
    [SYS_DEV]={SYS_ROOT,"dev",0,true},
    [SYS_DEV_BLOCK]={SYS_DEV,"block",0,true},
    [SYS_BLOCK]={SYS_ROOT,"block",0,true},
    [SYS_VIRTUAL]={SYS_DEVICES,"virtual",0,true},
    [SYS_VIRTUAL_BLOCK]={SYS_VIRTUAL,"block",0,true}
};
/* Dynamic sysfs block identities are incarnations issued by the real block
 * class registry. No symlink, geometry or partition appears before its owner
 * registers, and stale inodes cannot become another disk after slot reuse. */
#define SYS_BLOCK_INO_BASE (UINT64_C(1)<<56)
#define SYS_BLOCK_INO_STRIDE 32U
enum {
    SB_DIR, SB_DEV, SB_SIZE, SB_RO, SB_REMOVABLE, SB_UEVENT,
    SB_QUEUE, SB_LOGICAL, SB_PHYSICAL, SB_PARTITION, SB_START,
    SB_ROOT_LINK, SB_DEV_LINK
};
struct sys_block_attr {const char *name; unsigned kind,parent;};
static const struct sys_block_attr sys_block_attrs[]={
    {"dev",SB_DEV,SB_DIR},{"size",SB_SIZE,SB_DIR},
    {"ro",SB_RO,SB_DIR},{"removable",SB_REMOVABLE,SB_DIR},
    {"uevent",SB_UEVENT,SB_DIR},{"queue",SB_QUEUE,SB_DIR},
    {"partition",SB_PARTITION,SB_DIR},{"start",SB_START,SB_DIR},
    {"logical_block_size",SB_LOGICAL,SB_QUEUE},
    {"physical_block_size",SB_PHYSICAL,SB_QUEUE}
};
static uint64_t sys_block_ino(const struct nas_block_device *dev,unsigned kind) {
    return SYS_BLOCK_INO_BASE+dev->incarnation*SYS_BLOCK_INO_STRIDE+kind;
}
static const struct nas_block_device *sys_block_decode(
    const struct nas_fs_node *node,unsigned *kind) {
    if(!node || node->ino<SYS_BLOCK_INO_BASE)return NULL;
    uint64_t delta=node->ino-SYS_BLOCK_INO_BASE;
    uint64_t epoch=delta/SYS_BLOCK_INO_STRIDE;
    unsigned type=(unsigned)(delta%SYS_BLOCK_INO_STRIDE);
    if(type>SB_DEV_LINK)return NULL;
    for(unsigned i=0;;i++) {
        const struct nas_block_device *dev=nas_block_at(i);
        if(!dev)return NULL;
        if(dev->incarnation==epoch) {
            if(kind)*kind=type;
            return dev;
        }
    }
}
/* All text is built from registered device geometry, not from a boot image,
 * lsblk-specific string or a second device inventory. */
static int sys_append(char dst[256],unsigned *length,const char *str) {
    size_t n=strlen(str);
    if(n>255U-*length)return -ENAMETOOLONG;
    memcpy(dst+*length,str,n);
    *length+=(unsigned)n;
    dst[*length]=0;
    return 0;
}
static int sys_append_uint(char dst[256],unsigned *length,uint64_t number) {
    char digits[20];unsigned n=0;
    do {digits[n++]=(char)('0'+number%10);number/=10;} while(number);
    if(n>255U-*length)return -ENAMETOOLONG;
    while(n)dst[(*length)++]=digits[--n];
    dst[*length]=0;
    return 0;
}
static int sys_block_dev_name(const struct nas_block_device *dev,char out[256]) {
    unsigned n=0;out[0]=0;
    if(sys_append_uint(out,&n,dev->rdev>>8) || sys_append(out,&n,":") ||
       sys_append_uint(out,&n,dev->rdev&255U))return -ENAMETOOLONG;
    return 0;
}
static int sys_block_text(const struct nas_block_device *dev,unsigned kind,
                          char out[256]) {
    unsigned n=0;out[0]=0;
    switch(kind) {
    case SB_DEV:
        if(sys_block_dev_name(dev,out))return -ENAMETOOLONG;
        n=(unsigned)strlen(out);
        return sys_append(out,&n,"\n");
    case SB_SIZE:
        if(sys_append_uint(out,&n,dev->sectors*(dev->sector_bytes/512U)))return -ENAMETOOLONG;
        return sys_append(out,&n,"\n");
    case SB_RO:case SB_REMOVABLE:
        return sys_append(out,&n,kind==SB_RO && dev->read_only?"1\n":"0\n");
    case SB_LOGICAL:case SB_PHYSICAL:
        if(sys_append_uint(out,&n,dev->sector_bytes))return -ENAMETOOLONG;
        return sys_append(out,&n,"\n");
    case SB_PARTITION:
        if(!dev->parent || !dev->partition_number)return -ENOENT;
        if(sys_append_uint(out,&n,dev->partition_number))return -ENAMETOOLONG;
        return sys_append(out,&n,"\n");
    case SB_START:
        if(!dev->parent)return -ENOENT;
        if(sys_append_uint(out,&n,dev->start_lba*(dev->sector_bytes/512U)))return -ENAMETOOLONG;
        return sys_append(out,&n,"\n");
    case SB_UEVENT:
        if(sys_append(out,&n,"MAJOR=") ||
           sys_append_uint(out,&n,dev->rdev>>8) ||
           sys_append(out,&n,"\nMINOR=") ||
           sys_append_uint(out,&n,dev->rdev&255U) ||
           sys_append(out,&n,"\nDEVNAME=") ||
           sys_append(out,&n,dev->name) ||
           sys_append(out,&n,dev->parent?"\nDEVTYPE=partition\n":"\nDEVTYPE=disk\n"))
            return -ENAMETOOLONG;
        return 0;
    default:return -EINVAL;
    }
}
static void sys_block_fill(const struct nas_block_device *dev,unsigned kind,
                           struct nas_fs_node *out) {
    bool dir=kind==SB_DIR || kind==SB_QUEUE;
    bool symlink=kind==SB_ROOT_LINK || kind==SB_DEV_LINK;
    char textbuf[256]={0};
    int length=dir||symlink?0:sys_block_text(dev,kind,textbuf);
    *out=(struct nas_fs_node){.ino=sys_block_ino(dev,kind),
        .mode=dir?0040555U:symlink?0120777U:0100444U,
        .size=!dir && !symlink && length>=0?(uint64_t)strlen(textbuf):0,
        .nlink=dir?2:1,.is_dir=dir,.is_symlink=symlink};
    if(dir||symlink)out->size=0;
}
static bool sys_block_attr_visible(const struct nas_block_device *dev,
                                   unsigned kind) {
    if(kind==SB_QUEUE)return !dev->parent;
    if(kind==SB_PARTITION)return dev->parent && dev->partition_number;
    if(kind==SB_START)return !!dev->parent;
    return (kind!=SB_LOGICAL && kind!=SB_PHYSICAL) || !dev->parent;
}
/* Index once by registered class children, with shared logic for lookup and
 * readdir; the result never derives from a hardcoded virtio device count. */
static bool sys_block_child_at(const struct nas_block_device *dev,
                               unsigned dir_kind,uint64_t index,
                               const struct nas_block_device **child,
                               unsigned *kind,const char **name) {
    for(unsigned i=0;i<sizeof(sys_block_attrs)/sizeof(sys_block_attrs[0]);i++) {
        const struct sys_block_attr *a=&sys_block_attrs[i];
        if(a->parent!=dir_kind || !sys_block_attr_visible(dev,a->kind))continue;
        if(index--)continue;
        *child=dev;*kind=a->kind;*name=a->name;return true;
    }
    if(dir_kind==SB_DIR && !dev->parent)for(unsigned i=0;;i++) {
        const struct nas_block_device *other=nas_block_at(i);
        if(!other)break;
        if(other->parent!=dev)continue;
        if(index--)continue;
        *child=other;*kind=SB_DIR;*name=other->name;return true;
    }
    return false;
}
static uint64_t sys_memory_ino(uint64_t block,unsigned kind) {
    return SYS_MEMORY_INO_BASE+block*SYS_MEMORY_INO_STRIDE+kind;
}
static bool sys_memory_decode(const struct nas_fs_node *node,
                              uint64_t *block,unsigned *kind) {
    if(!node || node->ino<SYS_MEMORY_INO_BASE)return false;
    uint64_t delta=node->ino-SYS_MEMORY_INO_BASE;
    uint64_t number=delta/SYS_MEMORY_INO_STRIDE;
    if(!nas_memory_block_present(number))return false;
    if(block)*block=number;
    if(kind)*kind=(unsigned)(delta%SYS_MEMORY_INO_STRIDE);
    return true;
}
static void sys_memory_fill(uint64_t block,unsigned kind,
                            struct nas_fs_node *out) {
    bool directory=kind==SYS_MEMORY_DIR;
    *out=(struct nas_fs_node){.ino=sys_memory_ino(block,kind),
        .nlink=directory?2:1,.mode=directory?0040555U:0100444U,
        .size=directory?0:kind==SYS_MEMORY_STATE?7:2,
        .is_dir=directory};
}
static bool sys_memory_parse(const char *name,uint64_t *block) {
    if(strncmp(name,"memory",6) || name[6]<'0' || name[6]>'9' ||
       (name[6]=='0' && name[7]))return false;
    uint64_t value=0;
    for(const char *p=name+6;*p;p++) {
        if(*p<'0' || *p>'9' || value>(UINT64_MAX-(unsigned)(*p-'0'))/10)
            return false;
        value=value*10+(unsigned)(*p-'0');
    }
    *block=value;
    return true;
}
static void sys_memory_name(uint64_t block,char name[32]) {
    char digits[20];unsigned count=0;
    do {digits[count++]=(char)('0'+block%10);block/=10;} while(block);
    memcpy(name,"memory",6);
    for(unsigned i=0;i<count;i++)name[6+i]=digits[count-1-i];
    name[6+count]=0;
}
static int sys_id(const struct nas_fs_node *node) {
    if(!node || node->ino<SYS_INO_BASE ||
       node->ino>=SYS_INO_BASE+SYS_NODE_COUNT)return -1;
    return (int)(node->ino-SYS_INO_BASE);
}
static void sys_fill(unsigned id,struct nas_fs_node *out) {
    const struct sys_entry *entry=&sys_entries[id];
    *out=(struct nas_fs_node){.ino=SYS_INO_BASE+id,
        .nlink=entry->directory?2:1,
        .mode=entry->directory?0040555U:0100444U,
        .size=entry->contents?strlen(entry->contents):0,
        .is_dir=entry->directory};
}
static int sys_root(const struct nas_vfs_mount *mount,
                    struct nas_fs_node *out) {
    (void)mount;
    if(!out)return -EINVAL;
    sys_fill(SYS_ROOT,out);
    return 0;
}
static int sys_child(const struct nas_fs_node *parent,const char *name,
                     struct nas_fs_node *out) {
    int id=sys_id(parent);
    if(!parent || !parent->is_dir)return -ENOTDIR;
    if(!name || !*name || !out || strchr(name,'/'))return -EINVAL;
    unsigned kind;
    if(id<0) {
        const struct nas_block_device *dev=sys_block_decode(parent,&kind);
        if(dev) {
            if(kind!=SB_DIR && kind!=SB_QUEUE)return -ENOTDIR;
            for(uint64_t i=0;;i++) {
                const struct nas_block_device *child;
                unsigned childkind;const char *childname;
                if(!sys_block_child_at(dev,kind,i,&child,&childkind,&childname))break;
                if(!strcmp(childname,name)) {
                    sys_block_fill(child,childkind,out);
                    return 0;
                }
            }
            return -ENOENT;
        }
        if(!sys_memory_decode(parent,0,&kind) || kind!=SYS_MEMORY_DIR)
            return -ENOTDIR;
        if(!strcmp(name,"state"))kind=SYS_MEMORY_STATE;
        else if(!strcmp(name,"removable"))kind=SYS_MEMORY_REMOVABLE;
        else return -ENOENT;
        uint64_t block;
        if(!sys_memory_decode(parent,&block,0))return -ESTALE;
        sys_memory_fill(block,kind,out);
        return 0;
    }
    for(unsigned i=1;i<SYS_NODE_COUNT;i++)
        if(sys_entries[i].parent==(unsigned)id &&
           !strcmp(name,sys_entries[i].name)) {
            sys_fill(i,out);
            return 0;
        }
    if(id==SYS_MEMORY) {
        uint64_t block;
        if(sys_memory_parse(name,&block) && nas_memory_block_present(block)) {
            sys_memory_fill(block,SYS_MEMORY_DIR,out);
            return 0;
        }
    }
    if(id==SYS_VIRTUAL_BLOCK || id==SYS_BLOCK || id==SYS_DEV_BLOCK) {
        for(unsigned i=0;;i++) {
            const struct nas_block_device *dev=nas_block_at(i);
            if(!dev)break;
            if(id==SYS_VIRTUAL_BLOCK || id==SYS_BLOCK) {
                if(dev->parent || strcmp(name,dev->name))continue;
                sys_block_fill(dev,id==SYS_BLOCK?SB_ROOT_LINK:SB_DIR,out);
                return 0;
            }
            char devname[256];
            if(!sys_block_dev_name(dev,devname) && !strcmp(name,devname)) {
                sys_block_fill(dev,SB_DEV_LINK,out);
                return 0;
            }
        }
    }
    return -ENOENT;
}
static int sys_read_dir(const struct nas_fs_node *node,uint64_t cursor,
                        struct nas_vfs_dirent *out,unsigned maximum) {
    int id=sys_id(node);
    if(!node || !node->is_dir || !out || !maximum)return -EINVAL;
    uint64_t block;unsigned kind;
    if(id<0) {
        const struct nas_block_device *dev=sys_block_decode(node,&kind);
        if(dev) {
            if(kind!=SB_DIR && kind!=SB_QUEUE)return -ENOTDIR;
            unsigned count=0;
            for(;count<maximum;count++,cursor++) {
                const char *name=".";
                const struct nas_block_device *child=dev;
                unsigned childkind=kind;
                uint64_t ino=sys_block_ino(dev,kind);
                unsigned dtype=4;
                if(cursor==1) {
                    name="..";
                    if(kind==SB_QUEUE)childkind=SB_DIR;
                    else if(dev->parent)child=dev->parent;
                    else ino=SYS_INO_BASE+SYS_VIRTUAL_BLOCK;
                    if(ino!=SYS_INO_BASE+SYS_VIRTUAL_BLOCK)
                        ino=sys_block_ino(child,childkind);
                } else if(cursor>=2) {
                    if(!sys_block_child_at(dev,kind,cursor-2,&child,&childkind,&name))
                        break;
                    ino=sys_block_ino(child,childkind);
                    dtype=childkind==SB_QUEUE || childkind==SB_DIR?4:8;
                }
                int rc=fill_dirent(&out[count],name,ino,dtype);
                if(rc)return rc;
            }
            return (int)count;
        }
        if(!sys_memory_decode(node,&block,&kind) || kind!=SYS_MEMORY_DIR)
            return -EINVAL;
        unsigned count=0;
        for(;count<maximum;count++,cursor++) {
            const char *name;
            uint64_t child=node->ino;
            unsigned dtype=4;
            if(cursor==0)name=".";
            else if(cursor==1) {name="..";child=SYS_INO_BASE+SYS_MEMORY;}
            else if(cursor==2) {
                name="state";child=sys_memory_ino(block,SYS_MEMORY_STATE);
                dtype=8;
            } else if(cursor==3) {
                name="removable";child=sys_memory_ino(block,SYS_MEMORY_REMOVABLE);
                dtype=8;
            } else break;
            int rc=fill_dirent(&out[count],name,child,dtype);
            if(rc)return rc;
        }
        return (int)count;
    }
    unsigned count=0;
    for(;count<maximum;count++,cursor++) {
        unsigned child=(unsigned)id;
        const char *name=".";
        char generated_name[256];
        if(cursor==1) {
            name="..";
            child=sys_entries[id].parent;
        } else if(cursor>=2) {
            uint64_t skip=cursor-2;
            bool found=false;
            for(unsigned i=1;i<SYS_NODE_COUNT;i++) {
                if(sys_entries[i].parent!=(unsigned)id)continue;
                if(skip--!=0)continue;
                child=i;
                name=sys_entries[i].name;
                found=true;
                break;
            }
            if(!found && id==SYS_MEMORY) {
                if(!nas_memory_block_index_at(skip,&block))break;
                sys_memory_name(block,generated_name);
                int rc=fill_dirent(&out[count],generated_name,
                                   sys_memory_ino(block,SYS_MEMORY_DIR),4);
                if(rc)return rc;
                continue;
            }
            if(!found && (id==SYS_VIRTUAL_BLOCK || id==SYS_BLOCK ||
                          id==SYS_DEV_BLOCK)) {
                for(unsigned i=0;;i++) {
                    const struct nas_block_device *dev=nas_block_at(i);
                    if(!dev)break;
                    if(id!=SYS_DEV_BLOCK && dev->parent)continue;
                    if(skip--)continue;
                    if(id==SYS_DEV_BLOCK) {
                        if(sys_block_dev_name(dev,generated_name))return -ENAMETOOLONG;
                        name=generated_name;
                    } else name=dev->name;
                    unsigned dtype=id==SYS_VIRTUAL_BLOCK?4:10;
                    unsigned nodekind=id==SYS_VIRTUAL_BLOCK?SB_DIR:
                        id==SYS_BLOCK?SB_ROOT_LINK:SB_DEV_LINK;
                    int rc=fill_dirent(&out[count],name,
                        sys_block_ino(dev,nodekind),dtype);
                    if(rc)return rc;
                    found=true;
                    break;
                }
                if(!found)break;
                continue;
            }
            if(!found)break;
        }
        int rc=fill_dirent(&out[count],name,SYS_INO_BASE+child,
                           sys_entries[child].directory?4:8);
        if(rc)return rc;
    }
    return (int)count;
}
static int64_t sys_read_inode(const struct nas_fs_node *node,uint64_t offset,
                              uint8_t *dst,uint64_t length) {
    int id=sys_id(node);
    if(!dst && length)return -EINVAL;
    const char *content;
    if(id<0) {
        unsigned blockkind;
        const struct nas_block_device *dev=sys_block_decode(node,&blockkind);
        if(dev) {
            if(blockkind==SB_DIR || blockkind==SB_QUEUE)return -EISDIR;
            if(blockkind==SB_ROOT_LINK || blockkind==SB_DEV_LINK)return -EINVAL;
            char blocktext[256];
            int rc=sys_block_text(dev,blockkind,blocktext);
            if(rc)return rc;
            uint64_t size=strlen(blocktext);
            if(offset>=size)return 0;
            if(length>size-offset)length=size-offset;
            memcpy(dst,blocktext+offset,(size_t)length);
            return (int64_t)length;
        }
        unsigned kind;
        if(!sys_memory_decode(node,0,&kind))return -EINVAL;
        if(kind==SYS_MEMORY_DIR)return -EISDIR;
        content=kind==SYS_MEMORY_STATE?"online\n":"0\n";
    } else {
        if(sys_entries[id].directory)return -EISDIR;
        content=sys_entries[id].contents;
    }
    if(!content)return -EIO;
    uint64_t size=strlen(content);
    if(offset>=size)return 0;
    if(length>size-offset)length=size-offset;
    memcpy(dst,content+offset,(size_t)length);
    return (int64_t)length;
}
static int sys_readlink(const struct nas_fs_node *node,char target[256]) {
    unsigned kind;
    const struct nas_block_device *dev=sys_block_decode(node,&kind);
    if(!dev || (kind!=SB_ROOT_LINK && kind!=SB_DEV_LINK))return -EINVAL;
    unsigned n=0;target[0]=0;
    if(sys_append(target,&n,kind==SB_ROOT_LINK?
                  "../devices/virtual/block/":"../../devices/virtual/block/"))
        return -ENAMETOOLONG;
    if(kind==SB_DEV_LINK && dev->parent) {
        if(sys_append(target,&n,dev->parent->name) ||
           sys_append(target,&n,"/"))return -ENAMETOOLONG;
    }
    if(sys_append(target,&n,dev->name))return -ENAMETOOLONG;
    return (int)n;
}
static const struct nas_vfs_type sys_type={
    .name="sysfs",.exec_source_label="sysfs",
    .root=sys_root,.lookup_child=sys_child,
    .readlink=sys_readlink,.read_at=sys_read_inode,
    .read_dir=sys_read_dir,.read_only=true
};
static const struct nas_vfs_mount defaults[]={
    {.mountpoint="/",.type=&boot_type,.permanent=true},
    {.mountpoint="/dev",.type=&nas_devfs_type,
     .allow_missing_mountpoint=true,.permanent=true},
    {.mountpoint="/tmp",.type=&tmp_type,.fallback_on_miss=true,
     .allow_missing_mountpoint=true,.permanent=true},
    {.mountpoint="/proc",.type=&proc_type,.fallback_on_miss=true,
     .allow_missing_mountpoint=true,.permanent=true},
    {.mountpoint="/sys",.type=&sys_type,.fallback_on_miss=true,
     .allow_missing_mountpoint=true,.permanent=true}
};
void nas_vfs_register_default_mounts(void) {
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++)
        (void)nas_vfs_register_mount(&defaults[i]);
}
/* The external root was registered FIRST. Attach the existing pseudo
 * providers to its actual root Dentry; never keep a parent pin to bootfs. */
static struct nas_vfs_mount external_pseudos[
    sizeof(defaults)/sizeof(defaults[0])-1];
static bool external_pseudos_mounted;
int nas_vfs_register_pseudo_mounts(void) {
    if(external_pseudos_mounted)return 0;
    for(unsigned i=0;i<sizeof(external_pseudos)/sizeof(external_pseudos[0]);i++) {
        external_pseudos[i]=defaults[i+1];
        /* Never guest-detach the legacy bootfs mounts. Only the external
         * disk boot's final PID1 lifecycle is allowed to release these. */
        external_pseudos[i].permanent=false;
        int rc=nas_vfs_register_mount(&external_pseudos[i]);
        if(rc) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
            extern void nas_print(const char *);
            extern void nas_dec(uint64_t);
            nas_print("[M191 ROOT FAIL] pseudo-attach ");
            nas_print(external_pseudos[i].mountpoint);
            nas_print(" code=");nas_dec((uint64_t)(rc<0?-rc:rc));
            nas_print("\n");
#endif
            while(i>0) {
                if(nas_vfs_unregister_mount(&external_pseudos[--i]))
                    return -EIO;
            }
            return rc;
        }
    }
    external_pseudos_mounted=true;
    return 0;
}
int nas_vfs_shutdown_external_root_pseudos(void) {
    if(!external_pseudos_mounted)return -EINVAL;
    for(unsigned i=sizeof(external_pseudos)/sizeof(external_pseudos[0]);i>0;i--) {
        int rc=nas_vfs_unregister_mount(&external_pseudos[i-1]);
        if(rc) {
#ifdef NAS_ENABLE_M191_ALPINE_ROOT
            extern void nas_print(const char *);
            extern void nas_dec(uint64_t);
            nas_print("[M191 PSEUDO UNMOUNT ERROR] ");
            nas_print(external_pseudos[i-1].mountpoint);
            nas_print(" errno=");nas_dec((uint64_t)(rc<0?-rc:rc));
            nas_print("\n");
#endif
            return rc;
        }
    }
    external_pseudos_mounted=false;
    return 0;
}
