#include "vfs.h"
#include "lookup.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "procfs.h"
#include "../errno.h"
#include "../mm/frame.h"
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
    (void)mount;
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
static int tmp_mknod_node(const struct nas_fs_node *parent,const char *name,
                          uint64_t mode,struct nas_fs_node *out) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    if(!out || !name)return -EINVAL;
    if((mode&0170000ULL)!=0140000ULL)return -EOPNOTSUPP;
    if(tmp_find_child(parent->inode,name))return -EEXIST;
    /* The tmpfs namespace owner initializes S_IFSOCK before publishing
     * the dentry. Never expose a transient regular-file inode as the old
     * AF_UNIX bind path did. */
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
    (void)node;
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
static const struct nas_vfs_type tmp_type={
    .name="tmpfs",.exec_source_label="tmpfs",
    .root=tmp_root,.lookup_child=tmp_child,
    .last_inode_unpin=tmp_last_inode_unpin,.getattr=tmp_getattr,
    .statfs=tmp_statfs,
    .validate_inode=tmp_validate_inode,
    .readlink=tmp_readlink,
    .read_at=tmp_read,.read_size_is_eof=true,
    .read_dir=tmp_read_dir,.chmod=tmp_chmod_node,.chown=tmp_chown_node,
    .utimens=tmp_utimens_node,
    .create=tmp_create_node,.mknod=tmp_mknod_node,.write_at=tmp_write_node,
    .resize=tmp_resize_node,.sync=tmp_sync_node,
    .mkdir=tmp_mkdir_node,.unlink=tmp_unlink_node,.rename=tmp_rename_node,
    .link=tmp_link_node,.symlink=tmp_symlink_node,
    .unlink_open_safe=true,.rename_replace_open_safe=true
};
static void proc_fill(struct nas_proc_node p,struct nas_fs_node *out) {
    *out=(struct nas_fs_node){.ino=nas_proc_ino(p),
      .nlink=nas_proc_is_dir(p)?2:1,.mode=nas_proc_mode(p),
      .is_dir=nas_proc_is_dir(p)};
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
    if(!nas_proc_fd_link_from_ino(node->ino,&proc))return -ENOENT;
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
                  entry.kind==NAS_PROC_FD_LINK?10:8;
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
static const struct nas_vfs_mount defaults[]={
    {.mountpoint="/",.type=&boot_type,.permanent=true},
    {.mountpoint="/tmp",.type=&tmp_type,.fallback_on_miss=true,
     .allow_missing_mountpoint=true,.permanent=true},
    {.mountpoint="/proc",.type=&proc_type,.fallback_on_miss=true,
     .allow_missing_mountpoint=true,.permanent=true}
};
void nas_vfs_register_default_mounts(void) {
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++)
        (void)nas_vfs_register_mount(&defaults[i]);
}
