/* Genuine unmodified littlefs -> NaSeed VFS, read-only until VFS can
 * guarantee writable open/rename/unlink inode lifetime for every provider.
 * All flash operations use the same existing BlockDevice as upstream ext4. */
#include "vfs_port.h"
#include "../../kernel/fs/lookup.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include <stddef.h>
#include <stdint.h>

static struct nas_littlefs_vfs *context(const struct nas_vfs_mount *m) {
    return m ? m->private_data : NULL;
}
static const char *relative(const struct nas_littlefs_vfs *ctx,
                            const char *absolute) {
    if(!ctx || !absolute)return NULL;
    size_t len=strlen(ctx->mount_point);
    if(strncmp(ctx->mount_point,absolute,len))return NULL;
    return !absolute[len] ? "/" : absolute[len]=='/' ? absolute+len : NULL;
}
/* Unique monotonic VFS IDs, scoped to this immutable mount. They are
 * NOT exported littlefs on-disk IDs. No hash collisions or pointer leaks. */
static struct nas_littlefs_identity *intern(struct nas_littlefs_vfs *ctx,
                                            const char *path) {
    for(unsigned i=0;i<ctx->count;i++)
        if(!strcmp(ctx->ids[i].path,path))return &ctx->ids[i];
    if(ctx->count==NAS_LITTLEFS_IDENTITIES || strlen(path)>=256)return NULL;
    struct nas_littlefs_identity *id=&ctx->ids[ctx->count];
    strcpy(id->path,path);
    id->ino=(uint64_t)++ctx->count;
    return id;
}
static int look_path(const struct nas_vfs_mount *m,const char *absolute,
                     struct nas_fs_node *out) {
    struct nas_littlefs_vfs *ctx=context(m);
    if(!ctx || !ctx->mounted || !out)return -ENODEV;
    const char *path=relative(ctx,absolute);
    if(!path)return -ENOENT;
    struct lfs_info info={0};
    int rc=lfs_stat(&ctx->fs,path,&info);
    if(rc)return rc;
    struct nas_littlefs_identity *id=intern(ctx,path);
    if(!id)return -ENOSPC;
    if(info.type!=LFS_TYPE_REG && info.type!=LFS_TYPE_DIR)return -EIO;
    bool dir=info.type==LFS_TYPE_DIR;
    *out=(struct nas_fs_node){
        .inode=id,.ino=id->ino,.nlink=dir?2:1,
        .size=dir?0:info.size,.mode=dir?0040555U:0100444U,
        .is_dir=dir
    };
    return 0;
}
static int look_root(const struct nas_vfs_mount *m,struct nas_fs_node *out) {
    struct nas_littlefs_vfs *ctx=context(m);
    if(!ctx)return -ENODEV;
    return look_path(m,ctx->mount_point,out);
}
static int look_child(const struct nas_fs_node *parent,const char *name,
                      struct nas_fs_node *out) {
    if(!parent || !parent->is_dir || !parent->mount || !parent->inode ||
       !name || !name[0] || !out)return -ENOTDIR;
    struct nas_littlefs_vfs *ctx=context(parent->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    const struct nas_littlefs_identity *dir=parent->inode;
    size_t n=strlen(dir->path),m=strlen(name);
    if(n+(n>1?1U:0U)+m>=256)return -ENAMETOOLONG;
    char local[256];
    memcpy(local,dir->path,n);
    if(n>1)local[n++]='/';
    memcpy(local+n,name,m+1);
    struct lfs_info info={0};
    int rc=lfs_stat(&ctx->fs,local,&info);
    if(rc)return rc;
    struct nas_littlefs_identity *id=intern(ctx,local);
    if(!id)return -ENOSPC;
    if(info.type!=LFS_TYPE_REG && info.type!=LFS_TYPE_DIR)return -EIO;
    bool is_dir=info.type==LFS_TYPE_DIR;
    *out=(struct nas_fs_node){
        .inode=id,.ino=id->ino,.nlink=is_dir?2:1,
        .size=is_dir?0:info.size,.mode=is_dir?0040555U:0100444U,
        .is_dir=is_dir
    };
    return 0;
}
static int64_t read_at(const struct nas_fs_node *node,uint64_t off,
                        uint8_t *dst,uint64_t count) {
    if(!node || !node->inode || !node->mount)return -EINVAL;
    struct nas_littlefs_vfs *ctx=context(node->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    if(node->is_dir)return -EISDIR;
    if(!dst && count)return -EFAULT;
    if(off>=node->size || !count)return 0;
    if(off>INT32_MAX)return -EFBIG;
    if(count>node->size-off)count=node->size-off;
    if(count>UINT32_MAX)count=UINT32_MAX;
    const struct nas_littlefs_identity *id=node->inode;
    uint8_t cache[256];
    struct lfs_file_config cfg={.buffer=cache};
    lfs_file_t f={0};
    int rc=lfs_file_opencfg(&ctx->fs,&f,id->path,LFS_O_RDONLY,&cfg);
    if(rc)return rc;
    lfs_soff_t pos=lfs_file_seek(&ctx->fs,&f,(lfs_soff_t)off,LFS_SEEK_SET);
    lfs_ssize_t n=pos<0?pos:lfs_file_read(&ctx->fs,&f,dst,(lfs_size_t)count);
    rc=lfs_file_close(&ctx->fs,&f);
    return n<0?n:rc?rc:n;
}
static int readdir(const struct nas_fs_node *node,const char *opened_path,
                   uint64_t cursor,struct nas_vfs_dirent *out,unsigned limit) {
    (void)opened_path;
    if(!node || !node->mount || !node->inode || !out || !limit)
        return -EINVAL;
    if(!node->is_dir)return -ENOTDIR;
    struct nas_littlefs_vfs *ctx=context(node->mount);
    if(!ctx || !ctx->mounted)return -ENODEV;
    const struct nas_littlefs_identity *dirid=node->inode;
    lfs_dir_t dir={0};
    int rc=lfs_dir_open(&ctx->fs,&dir,dirid->path);
    if(rc)return rc;
    struct lfs_info e={0};
    uint64_t scanned=0;
    unsigned copied=0;
    while(copied<limit && (rc=lfs_dir_read(&ctx->fs,&dir,&e))>0) {
        if(scanned++<cursor)continue;
        uint64_t ino=0;
        if(!strcmp(e.name,"."))ino=node->ino;
        else {
            char child[256];
            if(!strcmp(e.name,"..")) {
                if(!strcmp(dirid->path,"/"))ino=node->ino;
                else {
                    size_t n=strlen(dirid->path);
                    while(n>1 && dirid->path[n-1]!='/')n--;
                    if(n<=1){child[0]='/';child[1]=0;}
                    else{memcpy(child,dirid->path,n-1);child[n-1]=0;}
                }
            } else {
                size_t n=strlen(dirid->path),m=strlen(e.name);
                if(n+(n>1?1:0)+m>=sizeof(child)){
                    rc=-ENAMETOOLONG;break;
                }
                memcpy(child,dirid->path,n);
                if(n>1)child[n++]='/';
                memcpy(child+n,e.name,m+1);
            }
            if(!ino) {
                struct nas_littlefs_identity *item=intern(ctx,child);
                if(!item){rc=-ENOSPC;break;}
                ino=item->ino;
            }
        }
        size_t n=strlen(e.name);
        if(!n || n>255 || !ino){rc=-EIO;break;}
        struct nas_vfs_dirent *d=&out[copied];
        *d=(struct nas_vfs_dirent){
            .ino=ino,.dtype=e.type==LFS_TYPE_DIR?4:8,
            .name_len=(uint8_t)n
        };
        memcpy(d->name,e.name,n+1);
        copied++;
    }
    int close_rc=lfs_dir_close(&ctx->fs,&dir);
    return rc<0?rc:close_rc?close_rc:(int)copied;
}
static const struct nas_vfs_type type={
    .name="littlefs",.exec_source_label="littlefs",
    .root=look_root,.lookup_child=look_child,
    .read_at=read_at,.read_dir=readdir,.read_only=true
};
int nas_littlefs_vfs_mount(struct nas_littlefs_vfs *ctx,
                           const char *device,const char *point) {
    if(!ctx || !device || !point || ctx->mounted || ctx->block.device)
        return -EINVAL;
    size_t len=strlen(point);
    if(!len || len>=sizeof(ctx->mount_point))return -ENAMETOOLONG;
    memcpy(ctx->mount_point,point,len+1);
    int rc=nas_littlefs_block_attach(&ctx->block,device);
    if(rc)return rc;
    ctx->block.config.read_buffer=ctx->read_cache;
    ctx->block.config.prog_buffer=ctx->write_cache;
    ctx->block.config.lookahead_buffer=ctx->lookahead;
    rc=lfs_mount(&ctx->fs,&ctx->block.config);
    if(rc)goto Fail;
    ctx->count=0;
    ctx->mount=(struct nas_vfs_mount){
        .mountpoint=ctx->mount_point,.type=&type,
        .read_only=true,.private_data=ctx
    };
    ctx->mounted=true;
    rc=nas_vfs_register_mount(&ctx->mount);
    if(!rc)return 0;
    ctx->mounted=false;
    (void)lfs_unmount(&ctx->fs);
Fail:
    nas_littlefs_block_detach(&ctx->block);
    return rc;
}
int nas_littlefs_vfs_unmount(struct nas_littlefs_vfs *ctx) {
    if(!ctx || !ctx->mounted)return -EINVAL;
    int rc=nas_vfs_unregister_mount(&ctx->mount);
    if(rc)return rc;
    rc=lfs_unmount(&ctx->fs);
    if(rc) {
        int restore=nas_vfs_register_mount(&ctx->mount);
        return restore?restore:rc;
    }
    ctx->mounted=false;
    ctx->count=0;
    nas_littlefs_block_detach(&ctx->block);
    return 0;
}
