/* Character-device names are a VFS filesystem, with device ownership left
 * entirely in the chrdev class registry. No Linux pathname dispatch here. */
#include "devfs.h"
#include "lookup.h"
#include "../drivers/core/chrdev.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>

enum { DEV_ROOT_INO=1, DEV_INPUT_INO=2, DEV_PTS_INO=3 };
static const char directory_tags[3];

static bool is_dir(const struct nas_fs_node *node,unsigned index) {
    return node && node->inode==&directory_tags[index] && node->is_dir &&
           node->ino==index+1U;
}
static void dir_inode(unsigned index,struct nas_fs_node *out) {
    *out=(struct nas_fs_node){
        .inode=&directory_tags[index],.ino=index+1U,
        .mode=0040755U,.nlink=2,.is_dir=true
    };
}
static void device_inode(const struct nas_chrdev_node *node,
                         struct nas_fs_node *out) {
    *out=(struct nas_fs_node){
        .inode=node,.ino=node->devfs_ino,.rdev=node->info.rdev,
        .mode=(uint32_t)node->info.mode,.size=node->info.size,
        .nlink=1,.is_dir=false
    };
}
static unsigned dir_number(const struct nas_fs_node *node) {
    for(unsigned i=0;i<3U;i++)if(is_dir(node,i))return i;
    return 3U;
}
/* Return a node's SINGLE direct-child component, or NULL if it belongs to
 * a different directory. Any future class can publish /dev/name without
 * teaching this filesystem about its FD kind or rdev. */
static const char *child_name(const struct nas_chrdev_node *node,
                              unsigned directory) {
    const char *prefix=directory==DEV_ROOT_INO-1U?"/dev/":
        directory==DEV_INPUT_INO-1U?"/dev/input/":"/dev/pts/";
    size_t length=strlen(prefix);
    if(strncmp(node->path,prefix,length))return NULL;
    const char *name=node->path+length;
    if(!*name || strchr(name,'/'))return NULL;
    return name;
}
static int root(const struct nas_vfs_mount *mount,
                struct nas_fs_node *out) {
    (void)mount;
    if(!out)return -EINVAL;
    dir_inode(0,out);
    return 0;
}
static int lookup_child(const struct nas_fs_node *parent,const char *name,
                        struct nas_fs_node *out) {
    if(!parent || !out || !name || !*name || strchr(name,'/'))return -EINVAL;
    unsigned dir=dir_number(parent);
    if(dir==3U)return -ENOTDIR;
    if(dir==0U && !strcmp(name,"input")) {dir_inode(1,out);return 0;}
    if(dir==0U && !strcmp(name,"pts")) {dir_inode(2,out);return 0;}
    for(unsigned i=0;;i++) {
        struct nas_chrdev_node *node=nas_chrdev_node_at(i);
        if(!node)break;
        const char *entry=child_name(node,dir);
        if(entry && !strcmp(entry,name)) {
            device_inode(node,out);
            return 0;
        }
    }
    return -ENOENT;
}
static int validate_inode(const struct nas_fs_node *node) {
    if(!node)return -EINVAL;
    if(dir_number(node)!=3U)return 0;
    const struct nas_chrdev_node *device=node->inode;
    if(!device || !device->registered || device->devfs_ino!=node->ino ||
       device->info.rdev!=node->rdev)return -ESTALE;
    /* An already-open inode survives the physical device disappearing.
     * Class presence is enforced when taking a fresh OpenFile pin. */
    return 0;
}
static int first_inode_pin(const struct nas_fs_node *node) {
    if(dir_number(node)!=3U)return 0;
    return nas_chrdev_node_pin((struct nas_chrdev_node *)(uintptr_t)node->inode);
}
static void last_inode_unpin(const struct nas_fs_node *node) {
    if(dir_number(node)==3U)
        nas_chrdev_node_put((struct nas_chrdev_node *)(uintptr_t)node->inode);
}
static int getattr(const struct nas_fs_node *node,struct nas_fs_node *out) {
    int rc=validate_inode(node);
    if(rc)return rc;
    *out=*node;
    if(dir_number(node)!=3U)return 0;
    const struct nas_chrdev_node *device=node->inode;
    /* Preserve mount/incarnation, but read mutable metadata from owner. */
    out->mode=(uint32_t)device->info.mode;
    out->size=device->info.size;
    return 0;
}
static int dirent(struct nas_vfs_dirent *out,const char *name,
                  uint64_t ino,unsigned dtype) {
    size_t length=strlen(name);
    if(!length || length>255U)return -ENAMETOOLONG;
    *out=(struct nas_vfs_dirent){
        .ino=ino,.dtype=(uint8_t)dtype,.name_len=(uint8_t)length
    };
    memcpy(out->name,name,length+1U);
    return 1;
}
static int entry_at(unsigned directory,uint64_t cursor,
                    struct nas_vfs_dirent *out) {
    if(cursor==0U)return dirent(out,".",directory+1U,4U);
    if(cursor==1U)return dirent(out,"..",DEV_ROOT_INO,4U);
    cursor-=2U;
    if(directory==0U) {
        if(cursor==0U)return dirent(out,"input",DEV_INPUT_INO,4U);
        if(cursor==1U)return dirent(out,"pts",DEV_PTS_INO,4U);
        cursor-=2U;
    }
    for(unsigned i=0;;i++) {
        struct nas_chrdev_node *node=nas_chrdev_node_at(i);
        if(!node)return 0;
        const char *name=child_name(node,directory);
        if(!name)continue;
        if(cursor--)continue;
        return dirent(out,name,node->devfs_ino,2U);
    }
}
static int read_dir(const struct nas_fs_node *node,uint64_t cursor,
                    struct nas_vfs_dirent *entries,unsigned maximum) {
    if(!node || !entries || !maximum)return -EINVAL;
    unsigned directory=dir_number(node);
    if(directory==3U)return -ENOTDIR;
    unsigned count=0;
    for(;count<maximum;count++,cursor++) {
        int rc=entry_at(directory,cursor,&entries[count]);
        if(rc<0)return rc;
        if(!rc)break;
    }
    return (int)count;
}
static int statfs(const struct nas_fs_node *node,struct nas_vfs_statfs *out) {
    (void)node;
    if(!out)return -EINVAL;
    *out=(struct nas_vfs_statfs){
        .magic=0x1373U,.block_size=4096U,.fragment_size=4096U,
        .name_max=255U
    };
    return 0;
}
const struct nas_vfs_type nas_devfs_type={
    .name="devfs",.exec_source_label="devfs",
    .root=root,.lookup_child=lookup_child,.read_dir=read_dir,
    .getattr=getattr,.validate_inode=validate_inode,
    .first_inode_pin=first_inode_pin,.last_inode_unpin=last_inode_unpin,
    .statfs=statfs,.read_only=true
};
