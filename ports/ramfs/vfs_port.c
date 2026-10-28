/* Independent upstream jkent/ramfs vector backend -> common inode VFS.
 * No fork of upstream and no filesystem-specific syscall/FD kind.
 * Deliberately fail closed on unsupported POSIX semantics (open-unlink,
 * rename-overwrite, symlinks, hardlinks). These are port/VFS contract tests. */
#include "vfs_port.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static struct nas_ramfs_vfs *context(const struct nas_vfs_mount *m) {
    return m ? (struct nas_ramfs_vfs *)m->private_data : NULL;
}
static int path_of(const struct nas_ramfs_identity *id,char path[256]) {
    if(!id || !id->active)return -ESTALE;
    if(!id->parent) {path[0]='/';path[1]=0;return 0;}
    char parent[256];
    int rc=path_of(id->parent,parent);
    return rc ? rc : nas_vfs_join_child(parent,id->name,path);
}
static struct nas_ramfs_identity *intern(struct nas_ramfs_vfs *ctx,
                                        struct nas_ramfs_identity *parent,
                                        const char *name,ramfs_entry_t *entry) {
    for(unsigned i=0;i<NAS_RAMFS_IDENTITIES;i++) {
        struct nas_ramfs_identity *id=&ctx->ids[i];
        if(id->active && id->parent==parent && id->entry==entry &&
           !strcmp(id->name,name))return id;
    }
    for(unsigned i=1;i<NAS_RAMFS_IDENTITIES;i++) {
        struct nas_ramfs_identity *id=&ctx->ids[i];
        if(id->active)continue;
        if(ctx->next_ino==UINT64_MAX)return NULL;
        *id=(struct nas_ramfs_identity){.entry=entry,.parent=parent,
           .ino=++ctx->next_ino,.active=true,
           .mode=ramfs_is_dir(entry)?0040755U:0100644U};
        memcpy(id->name,name,strlen(name)+1);
        return id;
    }
    return NULL;
}
static int fill(struct nas_ramfs_vfs *ctx,struct nas_ramfs_identity *id,
                struct nas_fs_node *out) {
    if(!ctx || !ctx->mounted || !id || !id->active || !out)return -ESTALE;
    ramfs_stat_t st={0};
    ramfs_stat(ctx->fs,id->entry,&st);
    bool dir=st.type==RAMFS_ENTRY_TYPE_DIR;
    *out=(struct nas_fs_node){.inode=id,.ino=id->ino,
        .mode=id->mode,.size=dir?0:(uint64_t)st.size,
        .nlink=dir?2:1,.is_dir=dir};
    return 0;
}
static int root(const struct nas_vfs_mount *m,struct nas_fs_node *out) {
    return fill(context(m),&context(m)->ids[0],out);
}
static int child(const struct nas_fs_node *parent,const char *name,
                 struct nas_fs_node *out) {
    if(!parent || !parent->is_dir || !parent->mount)return -ENOTDIR;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    struct nas_ramfs_identity *dir=(struct nas_ramfs_identity *)parent->inode;
    char base[256],path[256];
    int rc=path_of(dir,base);
    if(rc)return rc;
    rc=nas_vfs_join_child(base,name,path);
    if(rc)return rc;
    ramfs_entry_t *entry=ramfs_get_entry(ctx->fs,path);
    if(!entry)return -ENOENT;
    struct nas_ramfs_identity *id=intern(ctx,dir,name,entry);
    return id?fill(ctx,id,out):-ENOSPC;
}
static int validate(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->inode)return -ESTALE;
    struct nas_ramfs_vfs *ctx=context(node->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    const struct nas_ramfs_identity *id=node->inode;
    if(id<&ctx->ids[0] || id>=&ctx->ids[NAS_RAMFS_IDENTITIES] ||
       !id->active || id->ino!=node->ino)return -ESTALE;
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
static int directory_path(const struct nas_fs_node *node,char absolute[256]) {
    if(!node || !node->is_dir || !absolute)return -ENOTDIR;
    int rc=validate(node);
    if(rc)return rc;
    char local[256];
    rc=path_of((const struct nas_ramfs_identity *)node->inode,local);
    if(rc)return rc;
    const char *mp=node->mount->mountpoint;
    if(!strcmp(local,"/")) {
        if(strlen(mp)>=256)return -ENAMETOOLONG;
        memcpy(absolute,mp,strlen(mp)+1);return 0;
    }
    size_t n=strlen(mp),m=strlen(local);
    if(n+m>=256)return -ENAMETOOLONG;
    memcpy(absolute,mp,n);memcpy(absolute+n,local,m+1);
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
    while(skip-- && ramfs_readdir(dh)) {}
    while(written<limit && (entry=ramfs_readdir(dh))) {
        char *name=ramfs_get_name(entry);
        if(!name) {rc=-ENOMEM;break;}
        size_t len=strlen(name);
        if(!len || len>255 || strchr(name,'/')) {
            free(name);rc=-EIO;break;
        }
        struct nas_ramfs_identity *id=intern(ctx,dir,name,(ramfs_entry_t *)entry);
        if(!id) {free(name);rc=-ENOSPC;break;}
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
    char base[256];
    rc=path_of((const struct nas_ramfs_identity *)parent->inode,base);
    return rc?rc:nas_vfs_join_child(base,name,path);
}
static int create(const struct nas_fs_node *parent,const char *name,uint64_t mode) {
    (void)mode;
    char path[256];int rc=child_path(parent,name,path);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(ramfs_get_entry(ctx->fs,path))return -EEXIST;
    return ramfs_create(ctx->fs,path,O_RDWR)?0:-ENOMEM;
}
static int mkdir_node(const struct nas_fs_node *parent,const char *name,
                      uint64_t mode) {
    (void)mode;
    char path[256];int rc=child_path(parent,name,path);
    if(rc)return rc;
    struct nas_ramfs_vfs *ctx=context(parent->mount);
    if(ramfs_get_entry(ctx->fs,path))return -EEXIST;
    return ramfs_mkdir(ctx->fs,path)?0:-ENOMEM;
}
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
    if(!id)return -ENOSPC;
    /* Upstream frees an entry IMMEDIATELY on unlink/rmdir. Retain the
     * VFS inode lease contract by forbidding deletion while ANY File/cwd
     * owns the inode, not by silently patching the vendor implementation. */
    if(nas_vfs_inode_is_pinned(id))return -EBUSY;
    int removed=dir?ramfs_rmdir(entry):ramfs_unlink(entry);
    if(removed)return dir?-ENOTEMPTY:-EIO;
    id->active=false;id->entry=NULL;
    return 0;
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
    if(!id)return -ENOSPC;
    if(ramfs_rename(ctx->fs,from,to))return -EIO;
    id->parent=(struct nas_ramfs_identity *)new_parent->inode;
    memcpy(id->name,new_name,strlen(new_name)+1);
    return 0;
}
static const struct nas_vfs_type ramfs_type={
    .name="upstream-ramfs",.exec_source_label="RAMFS",
    .root=root,.lookup_child=child,.directory_path=directory_path,
    .getattr=getattr,.validate_inode=validate,.read_at=read_at,
    .read_dir=readdir_inode,.create=create,.mkdir=mkdir_node,
    .write_at=write_at,.resize=resize,.sync=sync_inode,
    .unlink=unlink_node,.rename=rename_node
};
int nas_ramfs_vfs_mount(struct nas_ramfs_vfs *ctx,const char *point) {
    if(!ctx || !point || ctx->mounted || ctx->fs)return -EINVAL;
    size_t length=strlen(point);
    if(length<2 || length>=256 || point[0]!='/')return -EINVAL;
    ramfs_fs_t *fs=ramfs_init();
    if(!fs)return -ENOMEM;
    ctx->fs=fs;ctx->next_ino=1;
    ctx->ids[0]=(struct nas_ramfs_identity){
        .entry=ramfs_get_entry(fs,"/"),.ino=1,.mode=0040755U,.active=true
    };
    ctx->mount=(struct nas_vfs_mount){
        .mountpoint=point,.type=&ramfs_type,.private_data=ctx
    };
    ctx->mounted=true;
    int rc=nas_vfs_register_mount(&ctx->mount);
    if(!rc)return 0;
    ctx->mounted=false;ctx->fs=NULL;
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
    memset(ctx->ids,0,sizeof(ctx->ids));
    return 0;
}
