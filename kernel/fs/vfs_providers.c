#include "vfs.h"
#include "lookup.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "procfs.h"
#include "../errno.h"
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

static int boot_lookup(const struct nas_vfs_mount *mount,
                       const char *path,struct nas_fs_node *out) {
    (void)mount;
    const struct bootfs_entry *e=bootfs_find(path);
    if(!e)return -ENOENT;
    *out=(struct nas_fs_node){.inode=e,.data=e->data,.size=e->size,
      .ino=e->ino,.nlink=bootfs_is_dir(e)?2:e->nlink,
      .mtime_sec=e->mtime_sec,.atime_sec=e->mtime_sec,
      .ctime_sec=e->mtime_sec,
      .mode=e->mode,.is_dir=bootfs_is_dir(e),.is_symlink=bootfs_is_link(e)};
    return 0;
}
static int boot_readlink(const struct nas_fs_node *node,const char *path,
                         char target[256]) {
    (void)path;
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

static int boot_read_dir(const struct nas_fs_node *node,const char *path,
                         uint64_t cursor,struct nas_vfs_dirent *out,
                         unsigned maximum) {
    (void)path;
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
    .name="bootfs",.exec_source_label="CPIO",.lookup=boot_lookup,.readlink=boot_readlink,
    .read_at=boot_read,.read_dir=boot_read_dir,.read_only=true
};
static int tmp_lookup(const struct nas_vfs_mount *mount,
                       const char *path,struct nas_fs_node *out) {
    (void)mount;
    struct nas_tmp_file *f=tmp_find(path);
    if(!f) {
        /* Synthetic /tmp overlay root: preserve the old tmpfs-only
         * directory listing while explicit fallback keeps CPIO lookups. */
        if(strcmp(path,"/tmp"))return -ENOENT;
        const struct bootfs_entry *root=bootfs_find("/tmp");
        if(!root || !bootfs_is_dir(root))return -ENOENT;
        *out=(struct nas_fs_node){.inode=root,.ino=root->ino,
                .nlink=2,.mode=root->mode,.is_dir=true};
        return 0;
    }
    *out=(struct nas_fs_node){.inode=f,.size=f->size,
      .ino=5000+tmp_number(f),.nlink=f->nlink?(f->is_dir?2:f->nlink):0,
      .atime_sec=f->atime_ns/1000000000ULL,
      .mtime_sec=f->mtime_ns/1000000000ULL,
      .ctime_sec=f->ctime_ns/1000000000ULL,
      .mode=f->mode,.is_dir=f->is_dir,.is_symlink=tmp_is_link(f)};
    return 0;
}
static int tmp_readlink(const struct nas_fs_node *node,const char *path,
                         char target[256]) {
    (void)path;
    const struct nas_tmp_file *f=node->inode;
    if(!f || f->size>=256)return -ENAMETOOLONG;
    int rc=tmp_link_target(f,target);
    return rc ? rc : (int)f->size;
}
static int64_t tmp_read(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *dst,uint64_t length) {
    return tmp_read_at(node->inode,offset,dst,length);
}
static int tmp_chmod_node(const struct nas_fs_node *node,uint64_t mode) {
    return tmp_chmod((struct nas_tmp_file *)(uintptr_t)node->inode,mode);
}
static int tmp_utimens_node(const struct nas_fs_node *node,uint64_t a,
                           uint64_t m,bool sa,bool sm) {
    return tmp_utimens((struct nas_tmp_file *)(uintptr_t)node->inode,a,m,sa,sm);
}

static int tmp_read_dir(const struct nas_fs_node *node,const char *path,
                        uint64_t cursor,struct nas_vfs_dirent *out,
                        unsigned maximum) {
    if(!node || !node->is_dir || !path || !out || !maximum)return -EINVAL;
    const bool root=!strcmp(path,"/tmp");
    const struct nas_tmp_file *directory=root?NULL:node->inode;
    if(!root && (!directory || !directory->is_dir))return -ENOTDIR;
    unsigned count=0;
    for(;count<maximum;count++,cursor++) {
        const char *name;
        uint64_t ino=node->ino;
        unsigned dtype=4;
        if(cursor==0)name=".";
        else if(cursor==1) {
            char parent[256];
            get_parent(path,parent);
            struct nas_tmp_file *p=tmp_find(parent);
            const struct bootfs_entry *b=bootfs_find(parent);
            ino=p?5000+tmp_number(p):b?b->ino:node->ino;
            name="..";
        } else {
            if(cursor-2>UINT32_MAX)break;
            const struct nas_tmp_dentry *entry=
                tmp_child_entry(path,(unsigned)(cursor-2));
            if(!entry)break;
            if(!entry->inode)return -EIO;
            ino=5000+tmp_number(entry->inode);
            dtype=file_dtype(entry->inode->mode);
            name=basename(entry->path);
        }
        int rc=fill_dirent(&out[count],name,ino,dtype);
        if(rc)return rc;
    }
    return (int)count;
}

static const struct nas_vfs_type tmp_type={
    .name="tmpfs",.exec_source_label="tmpfs",.lookup=tmp_lookup,.readlink=tmp_readlink,
    .read_at=tmp_read,.read_dir=tmp_read_dir,.chmod=tmp_chmod_node,.utimens=tmp_utimens_node
};
static int proc_lookup_node(const struct nas_vfs_mount *mount,
                       const char *path,struct nas_fs_node *out) {
    (void)mount;
    struct nas_proc_node p;
    if(!nas_proc_lookup(path,&p))return -ENOENT;
    *out=(struct nas_fs_node){.ino=nas_proc_ino(p),.nlink=nas_proc_is_dir(p)?2:1,
      .mode=nas_proc_mode(p),.is_dir=nas_proc_is_dir(p),
      .is_symlink=p.kind==NAS_PROC_FD_LINK};
    return 0;
}

static int proc_read_dir(const struct nas_fs_node *node,const char *path,
                         uint64_t cursor,struct nas_vfs_dirent *out,
                         unsigned maximum) {
    if(!node || !node->is_dir || !path || !out || !maximum)return -EINVAL;
    struct nas_proc_node directory;
    if(!nas_proc_lookup(path,&directory))return -ENOENT;
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

static const struct nas_vfs_type proc_type={
    .name="procfs",.exec_source_label="procfs",.lookup=proc_lookup_node,.read_dir=proc_read_dir,.read_only=true
};
static const struct nas_vfs_mount defaults[]={
    {.mountpoint="/",.type=&boot_type,.permanent=true},
    {.mountpoint="/tmp",.type=&tmp_type,.fallback_on_miss=true,.permanent=true},
    {.mountpoint="/proc",.type=&proc_type,.fallback_on_miss=true,.permanent=true}
};
void nas_vfs_register_default_mounts(void) {
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++)
        (void)nas_vfs_register_mount(&defaults[i]);
}
