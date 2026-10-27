#include "vfs.h"
#include "lookup.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "procfs.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
static int boot_lookup(const char *path,struct nas_fs_node *out) {
    const struct bootfs_entry *e=bootfs_find(path);
    if(!e)return -ENOENT;
    *out=(struct nas_fs_node){.inode=e,.data=e->data,.size=e->size,
      .mode=e->mode,.is_dir=bootfs_is_dir(e),.is_symlink=bootfs_is_link(e)};
    return 0;
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
static const struct nas_vfs_type boot_type={
    .name="bootfs",.exec_source_label="CPIO",.lookup=boot_lookup,.readlink=boot_readlink,
    .read_at=boot_read,.read_only=true
};
static int tmp_lookup(const char *path,struct nas_fs_node *out) {
    struct nas_tmp_file *f=tmp_find(path);
    if(!f)return -ENOENT;
    *out=(struct nas_fs_node){.inode=f,.size=f->size,
      .mode=f->mode,.is_dir=f->is_dir,.is_symlink=tmp_is_link(f)};
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
static int tmp_chmod_node(const struct nas_fs_node *node,uint64_t mode) {
    return tmp_chmod((struct nas_tmp_file *)(uintptr_t)node->inode,mode);
}
static int tmp_utimens_node(const struct nas_fs_node *node,uint64_t a,
                           uint64_t m,bool sa,bool sm) {
    return tmp_utimens((struct nas_tmp_file *)(uintptr_t)node->inode,a,m,sa,sm);
}
static const struct nas_vfs_type tmp_type={
    .name="tmpfs",.exec_source_label="tmpfs",.lookup=tmp_lookup,.readlink=tmp_readlink,
    .read_at=tmp_read,.chmod=tmp_chmod_node,.utimens=tmp_utimens_node
};
static int proc_lookup_node(const char *path,struct nas_fs_node *out) {
    struct nas_proc_node p;
    if(!nas_proc_lookup(path,&p))return -ENOENT;
    *out=(struct nas_fs_node){.mode=nas_proc_mode(p),.is_dir=nas_proc_is_dir(p)};
    return 0;
}
static const struct nas_vfs_type proc_type={
    .name="procfs",.exec_source_label="procfs",.lookup=proc_lookup_node,.read_only=true
};
static const struct nas_vfs_mount defaults[]={
    {.mountpoint="/",.type=&boot_type},
    {.mountpoint="/tmp",.type=&tmp_type,.fallback_on_miss=true},
    {.mountpoint="/proc",.type=&proc_type,.fallback_on_miss=true}
};
void nas_vfs_register_default_mounts(void) {
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++)
        (void)nas_vfs_register_mount(&defaults[i]);
}
