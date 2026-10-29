/* Independent upstream jkent/ramfs vector backend -> common inode VFS.
 * No fork of upstream and no filesystem-specific syscall/FD kind.
 * Deliberately fail closed on unsupported POSIX semantics (open-unlink,
 * rename-overwrite, symlinks, hardlinks). These are port/VFS contract tests. */
#include "vfs_port.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/mm/heap.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

/* The original vendor immediately frees entries on unlink, even if a VFS
 * File still owns the inode. Rather than forking it, keep unlinked-open
 * objects reachable ONLY to the backend under one private directory until
 * the existing VFS last_inode_unpin callback performs final reclamation.
 * This is an adapter-owned lifetime protocol, not a userspace filename. */
#define RAMFS_ORPHAN_DIR ".nas_ramfs_orphans"
static bool hidden_name(const struct nas_ramfs_identity *parent,
                        const struct nas_ramfs_vfs *ctx,const char *name) {
    return parent==&ctx->root_inode && !strcmp(name,RAMFS_ORPHAN_DIR);
}
static void orphan_basename(uint64_t ino,char name[18]) {
    static const char hex[]="0123456789abcdef";
    name[0]='o';
    for(unsigned i=0;i<16;i++)
        name[1+i]=hex[(unsigned)((ino>>((15U-i)*4U))&15U)];
    name[17]=0;
}

static struct nas_ramfs_vfs *context(const struct nas_vfs_mount *m) {
    return m ? (struct nas_ramfs_vfs *)m->private_data : NULL;
}
static int path_of(const struct nas_ramfs_identity *id,char path[256]) {
    if(!id)return -ESTALE;
    /* A valid 255-byte pathname can have 127 one-character components.
     * Recursing with a 256-byte parent buffer per component would exhaust
     * the kernel's 16 KiB stack. Count and fill backwards in constant stack. */
    size_t length=0;
    for(const struct nas_ramfs_identity *it=id;;it=it->parent) {
        if(!it || !it->active)return -ESTALE;
        if(!it->parent)break;
        size_t n=strnlen(it->name,sizeof(it->name));
        if(n>=sizeof(it->name))return -ENAMETOOLONG;
        if(!n || strchr(it->name,'/') || !strcmp(it->name,".") ||
           !strcmp(it->name,".."))return -EINVAL;
        if(n>=255U-length)return -ENAMETOOLONG;
        length+=n+1; /* one leading '/' per component */
    }
    if(!length) {path[0]='/';path[1]=0;return 0;}
    path[length]=0;
    for(const struct nas_ramfs_identity *it=id;it->parent;it=it->parent) {
        size_t n=strlen(it->name);
        length-=n;
        memcpy(path+length,it->name,n);
        path[--length]='/';
    }
    return 0;
}
/* One mount-local namespace identity graph, with no arbitrary 128-inode
 * limit and no separate first-party directory tree. The unmodified
 * upstream owns directories; this sidecar gives VFS stable generations.
 * Reuse of an unlinked identity MUST change ino to reject stale borrows. */
static struct nas_ramfs_identity *intern(struct nas_ramfs_vfs *ctx,
                                        struct nas_ramfs_identity *parent,
                                        const char *name,ramfs_entry_t *entry) {
    if(!ctx || !name || !name[0] || strlen(name)>=256 || !entry)return NULL;
    struct nas_ramfs_identity *unused=NULL;
    for(struct nas_ramfs_identity *id=ctx->identities;id;id=id->next) {
        if(id->active && id->parent==parent && id->entry==entry &&
           !strcmp(id->name,name))return id;
        if(!id->active && !unused)unused=id;
    }
    if(ctx->next_ino==UINT64_MAX)return NULL;
    if(!unused) {
        unused=nas_heap_calloc(1,sizeof(*unused));
        if(!unused)return NULL;
        unused->next=ctx->identities;
        ctx->identities=unused;
    }
    struct nas_ramfs_identity *next=unused->next;
    *unused=(struct nas_ramfs_identity){.next=next,
        .entry=entry,.parent=parent,.ino=++ctx->next_ino,.active=true,
        .mode=ramfs_is_dir(entry)?0040755U:0100644U};
    memcpy(unused->name,name,strlen(name)+1);
    return unused;
}
static int fill(struct nas_ramfs_vfs *ctx,struct nas_ramfs_identity *id,
                struct nas_fs_node *out) {
    if(!ctx || !ctx->mounted || !id || !id->active || !out)return -ESTALE;
    ramfs_stat_t st={0};
    ramfs_stat(ctx->fs,id->entry,&st);
    bool dir=st.type==RAMFS_ENTRY_TYPE_DIR;
    *out=(struct nas_fs_node){.inode=id,.ino=id->ino,
        .mode=id->mode,.size=dir?0:(uint64_t)st.size,
        .nlink=id->orphaned?0:(dir?2:1),.is_dir=dir};
    return 0;
}
static int root(const struct nas_vfs_mount *m,struct nas_fs_node *out) {
    struct nas_ramfs_vfs *ctx=context(m);
    return ctx ? fill(ctx,&ctx->root_inode,out) : -ENODEV;
}
static int child(const struct nas_fs_node *parent,const char *name,
                 struct nas_fs_node *out) {
    if(!parent || !parent->is_dir || !parent->mount)return -ENOTDIR;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    struct nas_ramfs_identity *dir=(struct nas_ramfs_identity *)parent->inode;
    if(hidden_name(dir,ctx,name))return -ENOENT;
    char base[256],path[256];
    int rc=path_of(dir,base);
    if(rc)return rc;
    rc=nas_vfs_join_child(base,name,path);
    if(rc)return rc;
    ramfs_entry_t *entry=ramfs_get_entry(ctx->fs,path);
    if(!entry)return -ENOENT;
    struct nas_ramfs_identity *id=intern(ctx,dir,name,entry);
    return id?fill(ctx,id,out):-ENOMEM;
}
static int validate(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->inode)return -ESTALE;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    const struct nas_ramfs_identity *id=node->inode;
    /* Validate pointer provenance BEFORE dereferencing an arbitrary stale
     * snapshot. Relational comparisons with unrelated pointers are UB. */
    bool owned=false;
    for(const struct nas_ramfs_identity *it=ctx->identities;it;it=it->next)
        if(it==id) {owned=true;break;}
    if(!owned || !id->active || id->ino!=node->ino)return -ESTALE;
    char path[256];
    int rc=path_of(id,path);
    if(rc)return rc;
    return ramfs_get_entry(ctx->fs,path)==id->entry?0:-ESTALE;
}
static int getattr(const struct nas_fs_node *node,struct nas_fs_node *out) {
    int rc=validate(node);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    struct nas_fs_node fresh;
    rc=fill(ctx,(struct nas_ramfs_identity *)node->inode,&fresh);
    if(rc)return rc;
    *out=*node;out->mode=fresh.mode;out->size=fresh.size;
    out->nlink=fresh.nlink;
    return 0;
}
static int64_t read_at(const struct nas_fs_node *node,uint64_t offset,
                       uint8_t *dst,uint64_t count) {
    int rc=validate(node);
    if(rc)return rc;
    if(node->is_dir)return -EISDIR;
    if(!count)return 0;
    if(!dst)return -EFAULT;
    if(offset>INT64_MAX || count>INT64_MAX)return -EFBIG;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    const struct nas_ramfs_identity *id=node->inode;
    ramfs_fh_t *fh=ramfs_open(ctx->fs,id->entry,O_RDONLY);
    if(!fh)return -ENOMEM;
    ssize_t pos=ramfs_seek(fh,(off_t)offset,SEEK_SET);
    ssize_t n=pos<0?-1:ramfs_read(fh,(char *)dst,(size_t)count);
    ramfs_close(fh);
    return n<0?-EIO:n;
}
static int64_t write_at(const struct nas_fs_node *node,uint64_t offset,
                        const uint8_t *src,uint64_t count) {
    int rc=validate(node);
    if(rc)return rc;
    if(node->is_dir)return -EISDIR;
    if(!count)return 0;
    if(!src)return -EFAULT;
    if(offset>INT64_MAX || count>INT64_MAX-offset)return -EFBIG;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    const struct nas_ramfs_identity *id=node->inode;
    ramfs_fh_t *fh=ramfs_open(ctx->fs,id->entry,O_RDWR);
    if(!fh)return -ENOMEM;
    ssize_t pos=ramfs_seek(fh,(off_t)offset,SEEK_SET);
    ssize_t n=pos<0?-1:ramfs_write(fh,(const char *)src,(size_t)count);
    ramfs_close(fh);
    return n<0?-ENOMEM:n;
}
static int resize(const struct nas_fs_node *node,uint64_t size) {
    int rc=validate(node);
    if(rc)return rc;
    if(node->is_dir)return -EISDIR;
    if(size>INT64_MAX)return -EFBIG;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    const struct nas_ramfs_identity *id=node->inode;
    if(size==0) {
        /* Upstream realloc(ptr,0) may return NULL as a successful free.
         * Its real O_TRUNC implementation has the portable zero behavior. */
        ramfs_fh_t *fh=ramfs_open(ctx->fs,id->entry,O_RDWR|O_TRUNC);
        if(!fh)return -ENOMEM;
        ramfs_close(fh);return 0;
    }
    return ramfs_truncate(ctx->fs,id->entry,(size_t)size)?-ENOMEM:0;
}
static int sync_inode(const struct nas_fs_node *node) {
    return validate(node); /* RAM is the final volatile storage. */
}
static int readdir_inode(const struct nas_fs_node *node,uint64_t cursor,
                         struct nas_vfs_dirent *out,unsigned limit) {
    int rc=validate(node);
    if(rc)return rc;
    if(!node->is_dir)return -ENOTDIR;
    if(!out || !limit)return -EINVAL;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    struct nas_ramfs_identity *dir=(struct nas_ramfs_identity *)node->inode;
    unsigned written=0;
    while(written<limit && cursor<2) {
        bool dot=cursor==0;
        const char *name=dot?".":"..";
        uint64_t ino=dot?dir->ino:(dir->parent?dir->parent->ino:dir->ino);
        out[written]=(struct nas_vfs_dirent){.ino=ino,.dtype=4,
            .name_len=(uint8_t)strlen(name)};
        memcpy(out[written].name,name,strlen(name)+1);
        cursor++;written++;
    }
    if(written==limit)return (int)written;
    ramfs_dh_t *dh=ramfs_opendir(ctx->fs,dir->entry);
    if(!dh)return -ENOMEM;
    uint64_t skip=cursor-2;
    const ramfs_entry_t *entry=NULL;
    if(skip>INT64_MAX) {ramfs_closedir(dh);return (int)written;}
    if(dir!=&ctx->root_inode) {
        /* The upstream vector backend owns a real O(1) directory seek;
         * do not rescan thousands of entries for every VFS getdents batch. */
        ramfs_seekdir(dh,(long)skip);
    } else {
        /* Skip the internal orphan directory from the VFS cursor namespace.
         * No process may enumerate, resolve or mutate this adapter name. */
        uint64_t visible=0;
        while(visible<skip && (entry=ramfs_readdir(dh))) {
            char *name=ramfs_get_name(entry);
            if(!name) {ramfs_closedir(dh);return -ENOMEM;}
            bool hidden=!strcmp(name,RAMFS_ORPHAN_DIR);
            free(name);
            if(!hidden)visible++;
        }
        if(visible<skip) {ramfs_closedir(dh);return (int)written;}
    }
    while(written<limit && (entry=ramfs_readdir(dh))) {
        char *name=ramfs_get_name(entry);
        if(!name) {rc=-ENOMEM;break;}
        if(dir==&ctx->root_inode && !strcmp(name,RAMFS_ORPHAN_DIR)) {
            free(name);continue;
        }
        size_t len=strlen(name);
        if(!len || len>255 || strchr(name,'/')) {
            free(name);rc=-EIO;break;
        }
        struct nas_ramfs_identity *id=intern(ctx,dir,name,(ramfs_entry_t *)entry);
        if(!id) {free(name);rc=-ENOMEM;break;}
        out[written]=(struct nas_vfs_dirent){
            .ino=id->ino,.dtype=ramfs_is_dir(entry)?4:8,
            .name_len=(uint8_t)len
        };
        memcpy(out[written].name,name,len+1);
        free(name);written++;
    }
    ramfs_closedir(dh);
    return rc<0?rc:(int)written;
}
static int child_path(const struct nas_fs_node *parent,const char *name,
                      char path[256]) {
    if(!parent || !parent->is_dir)return -ENOTDIR;
    int rc=validate(parent);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(hidden_name((const struct nas_ramfs_identity *)parent->inode,
                   ctx,name))return -EPERM;
    char base[256];
    rc=path_of((const struct nas_ramfs_identity *)parent->inode,base);
    return rc?rc:nas_vfs_join_child(base,name,path);
}
/* Provider-local permissions are metadata, not a reason to fork or
 * rewrite the original third-party directory/data backend. */
static int create(const struct nas_fs_node *parent,const char *name,uint64_t mode) {
    char path[256];int rc=child_path(parent,name,path);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(ramfs_get_entry(ctx->fs,path))return -EEXIST;
    ramfs_entry_t *entry=ramfs_create(ctx->fs,path,O_RDWR);
    if(!entry)return -ENOMEM;
    struct nas_ramfs_identity *id=intern(ctx,
        (struct nas_ramfs_identity *)parent->inode,name,entry);
    if(!id) {(void)ramfs_unlink(entry);return -ENOMEM;}
    id->mode=0100000U|((uint32_t)mode&07777U);
    return 0;
}
static int mkdir_node(const struct nas_fs_node *parent,const char *name,
                      uint64_t mode) {
    char path[256];int rc=child_path(parent,name,path);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(ramfs_get_entry(ctx->fs,path))return -EEXIST;
    ramfs_entry_t *entry=ramfs_mkdir(ctx->fs,path);
    if(!entry)return -ENOMEM;
    struct nas_ramfs_identity *id=intern(ctx,
        (struct nas_ramfs_identity *)parent->inode,name,entry);
    if(!id) {(void)ramfs_rmdir(entry);return -ENOMEM;}
    id->mode=0040000U|((uint32_t)mode&07777U);
    return 0;
}
static int chmod_node(const struct nas_fs_node *node,uint64_t mode) {
    int rc=validate(node);
    if(rc)return rc;
    struct nas_ramfs_identity *id=(struct nas_ramfs_identity *)node->inode;
    id->mode=(id->mode&0170000U)|((uint32_t)mode&07777U);
    return 0;
}
/* The original backend has no orphan API. For an open inode detach the
 * existing entry by vendor rename into an unnameable private directory.
 * Its entry address and data survive the rename, and the SAME VFS inode
 * identity is used by File after unlink; final VFS unpin reclaims it. */
static int unlink_node(const struct nas_fs_node *parent,const char *name,
                       uint64_t flags) {
    char path[256];int rc=child_path(parent,name,path);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    ramfs_entry_t *entry=ramfs_get_entry(ctx->fs,path);
    if(!entry)return -ENOENT;
    bool dir=ramfs_is_dir(entry);
    if(dir!=!!(flags&0x200U))return dir?-EISDIR:-ENOTDIR;
    struct nas_ramfs_identity *id=intern(ctx,
        (struct nas_ramfs_identity *)parent->inode,name,entry);
    if(!id)return -ENOMEM;
    if(nas_vfs_inode_is_pinned(id)) {
        /* Enforce empty-directory semantics BEFORE moving to a hidden
         * name; a nonempty open directory must not orphan its children. */
        if(dir) {
            ramfs_dh_t *dh=ramfs_opendir(ctx->fs,entry);
            if(!dh)return -ENOMEM;
            bool nonempty=ramfs_readdir(dh)!=NULL;
            ramfs_closedir(dh);
            if(nonempty)return -ENOTEMPTY;
        }
        char namebuf[18],hidden_path[256];
        orphan_basename(id->ino,namebuf);
        rc=nas_vfs_join_child("/" RAMFS_ORPHAN_DIR,
                              namebuf,hidden_path);
        if(rc)return rc;
        if(ramfs_get_entry(ctx->fs,hidden_path))return -EEXIST;
        if(ramfs_rename(ctx->fs,path,hidden_path))return -EIO;
        /* The old VFS inode stays live; only its unpublished backend
         * dentry name changes. No new File is allocated for an orphan. */
        id->parent=ctx->orphan_dir;
        memcpy(id->name,namebuf,sizeof(namebuf));
        id->orphaned=true;
        return 0;
    }
    int removed=dir?ramfs_rmdir(entry):ramfs_unlink(entry);
    if(removed)return dir?-ENOTEMPTY:-EIO;
    id->active=false;id->entry=NULL;
    return 0;
}
static void last_inode_unpin(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->inode)return;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    if(!ctx || !ctx->mounted)return;
    struct nas_ramfs_identity *id=(struct nas_ramfs_identity *)node->inode;
    if(!id->active || !id->orphaned || id->ino!=node->ino)return;
    /* VFS has already removed its LAST lease. An inaccessible orphan
     * cannot gain new links via public VFS namei; final release is safe. */
    int rc=ramfs_is_dir(id->entry)?ramfs_rmdir(id->entry):
                                    ramfs_unlink(id->entry);
    if(!rc) {
        id->entry=NULL;id->active=false;id->orphaned=false;
    }
}
static int rename_node(const struct nas_fs_node *old_parent,const char *old_name,
                       const struct nas_fs_node *new_parent,const char *new_name,
                       uint64_t flags) {
    if(flags)return -EINVAL;
    if(old_parent->mount!=new_parent->mount)return -EXDEV;
    char from[256],to[256];
    int rc=child_path(old_parent,old_name,from);
    if(rc)return rc;
    rc=child_path(new_parent,new_name,to);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(old_parent->mount);
    ramfs_entry_t *source=ramfs_get_entry(ctx->fs,from);
    if(!source)return -ENOENT;
    if(!strcmp(from,to))return 0;
    /* Original embedded ramfs has no atomic POSIX rename-overwrite.
     * Report the unsupported contract; never unlink victim first. */
    if(ramfs_get_entry(ctx->fs,to))return -EOPNOTSUPP;
    struct nas_ramfs_identity *id=intern(ctx,
        (struct nas_ramfs_identity *)old_parent->inode,old_name,source);
    if(!id)return -ENOMEM;
    if(ramfs_rename(ctx->fs,from,to))return -EIO;
    id->parent=(struct nas_ramfs_identity *)new_parent->inode;
    memcpy(id->name,new_name,strlen(new_name)+1);
    return 0;
}
static const struct nas_vfs_type ramfs_type={
    .name="upstream-ramfs",.exec_source_label="RAMFS",
    .root=root,.lookup_child=child,
    .getattr=getattr,.validate_inode=validate,.read_at=read_at,
    .last_inode_unpin=last_inode_unpin,
    .read_dir=readdir_inode,.create=create,.mkdir=mkdir_node,
    .chmod=chmod_node,
    .write_at=write_at,.resize=resize,.sync=sync_inode,
    .unlink=unlink_node,.rename=rename_node,
    .unlink_open_file_safe=true,.unlink_open_dir_safe=true
};
int nas_ramfs_vfs_mount(struct nas_ramfs_vfs *ctx,const char *point) {
    if(!ctx || !point || ctx->mounted || ctx->fs)return -EINVAL;
    size_t length=strlen(point);
    if(length<2 || length>=256 || point[0]!='/')return -EINVAL;
    ramfs_fs_t *fs=ramfs_init();
    if(!fs)return -ENOMEM;
    ctx->fs=fs;ctx->next_ino=1;
    ctx->root_inode=(struct nas_ramfs_identity){
        .entry=ramfs_get_entry(fs,"/"),.ino=1,.mode=0040755U,.active=true
    };
    ctx->identities=&ctx->root_inode;
    /* Vendor backend owns its private directory. Users never see it, and
     * each VFS mount has an independent orphan name and generation scope. */
    ramfs_entry_t *orphan_root=ramfs_mkdir(fs,"/" RAMFS_ORPHAN_DIR);
    if(!orphan_root) {
        ctx->identities=NULL;ctx->fs=NULL;
        ctx->root_inode=(struct nas_ramfs_identity){0};
        ramfs_deinit(fs);
        return -ENOMEM;
    }
    ctx->orphan_dir=intern(ctx,&ctx->root_inode,RAMFS_ORPHAN_DIR,
                            orphan_root);
    if(!ctx->orphan_dir) {
        ctx->identities=NULL;ctx->fs=NULL;
        ctx->root_inode=(struct nas_ramfs_identity){0};
        ramfs_deinit(fs);
        return -ENOMEM;
    }
    ctx->mount=(struct nas_vfs_mount){
        .mountpoint=point,.type=&ramfs_type,
        .allow_missing_mountpoint=true, /* internal early-boot /ramfs */
        .private_data=ctx
    };
    ctx->mounted=true;
    int rc=nas_vfs_register_mount(&ctx->mount);
    if(!rc)return 0;
    ctx->mounted=false;ctx->fs=NULL;
    struct nas_ramfs_identity *id=ctx->identities;
    while(id) {
        struct nas_ramfs_identity *next=id->next;
        if(id!=&ctx->root_inode)(void)nas_heap_free(id);
        id=next;
    }
    ctx->identities=NULL;ctx->orphan_dir=NULL;
    ctx->root_inode=(struct nas_ramfs_identity){0};
    ramfs_deinit(fs);
    return rc;
}
int nas_ramfs_vfs_unmount(struct nas_ramfs_vfs *ctx) {
    if(!ctx || !ctx->mounted)return -EINVAL;
    int rc=nas_vfs_unregister_mount(&ctx->mount);
    if(rc)return rc;
    ctx->mounted=false;
    ramfs_fs_t *fs=ctx->fs;ctx->fs=NULL;
    ramfs_deinit(fs);
    struct nas_ramfs_identity *id=ctx->identities;
    while(id) {
        struct nas_ramfs_identity *next=id->next;
        if(id!=&ctx->root_inode)(void)nas_heap_free(id);
        id=next;
    }
    ctx->identities=NULL;ctx->orphan_dir=NULL;
    ctx->root_inode=(struct nas_ramfs_identity){0};
    return 0;
}
