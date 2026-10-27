#include "vfs.h"
#include "lookup.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
#define NAS_VFS_MOUNT_LIMIT 16U
static const struct nas_vfs_mount *mounts[NAS_VFS_MOUNT_LIMIT];
static unsigned count;
static bool defaults_ready;
void nas_vfs_register_default_mounts(void);
static void ensure_defaults(void) {
    if (defaults_ready) return;
    defaults_ready = true;
    nas_vfs_register_default_mounts();
}
static bool mount_matches(const char *prefix, const char *path) {
    size_t n = strlen(prefix);
    if (n==1 && prefix[0]=='/') return path[0]=='/';
    return !strncmp(prefix,path,n) && (path[n]=='/' || path[n]==0);
}
int nas_vfs_register_mount(const struct nas_vfs_mount *mount) {
    ensure_defaults();
    if (!mount || !mount->type || !mount->type->lookup ||
        !mount->type->name || !mount->mountpoint ||
        mount->mountpoint[0]!='/') return -EINVAL;
    size_t len=strlen(mount->mountpoint);
    if (!len || len>=256 || (len>1 && mount->mountpoint[len-1]=='/'))
        return -EINVAL;
    for (unsigned i=0;i<count;i++)
        if (!strcmp(mounts[i]->mountpoint,mount->mountpoint))return -EEXIST;
    if (count==NAS_VFS_MOUNT_LIMIT)return -ENOSPC;
    mounts[count++]=mount;return 0;
}
int nas_vfs_lookup(const char *absolute,struct nas_fs_node *out) {
    if (!absolute || !out || absolute[0]!='/')return -EINVAL;
    ensure_defaults();
    unsigned seen=0;
    /* Longest mountpoint wins. Only the original /tmp overlay may fall
     * back to the CPIO root, and that compatibility is explicit in mount. */
    while (seen < (1U<<count)-1U) {
        const struct nas_vfs_mount *best=0;
        size_t longest=0;
        unsigned index=0;
        for(unsigned i=0;i<count;i++) {
            if (seen & (1U<<i))continue;
            const struct nas_vfs_mount *m=mounts[i];
            if (!mount_matches(m->mountpoint,absolute))continue;
            size_t n=strlen(m->mountpoint);
            if (!best || n>longest) {best=m;index=i;longest=n;}
        }
        if (!best)break;
        seen|=1U<<index;
        *out=(struct nas_fs_node){0};
        int rc=best->type->lookup(absolute,out);
        if(!rc) {out->filesystem=best->type;return 0;}
        if(rc!=-ENOENT || !best->fallback_on_miss)return rc;
    }
    return -ENOENT;
}
int nas_vfs_readlink(const struct nas_fs_node *node,char target[256]) {
    if (!node || !node->filesystem || !node->is_symlink ||
        !node->filesystem->readlink)return -EINVAL;
    return node->filesystem->readlink(node,target);
}
int64_t nas_vfs_read_at(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *dst,uint64_t length) {
    if(!node || !node->filesystem)return -EINVAL;
    if(node->is_dir)return -EISDIR;
    if(!dst && length)return -EINVAL;
    if(!node->filesystem->read_at)return -EOPNOTSUPP;
    return node->filesystem->read_at(node,offset,dst,length);
}
bool nas_vfs_can_write(const struct nas_fs_node *node) {
    return node && node->filesystem && !node->filesystem->read_only;
}
int nas_vfs_chmod(const struct nas_fs_node *node,uint64_t mode) {
    if(!node || !node->filesystem)return -EINVAL;
    if(node->filesystem->read_only)return -EROFS;
    if(!node->filesystem->chmod)return -EOPNOTSUPP;
    return node->filesystem->chmod(node,mode);
}
int nas_vfs_utimens(const struct nas_fs_node *node,uint64_t atime,
                    uint64_t mtime,bool set_atime,bool set_mtime) {
    if(!node || !node->filesystem)return -EINVAL;
    if(node->filesystem->read_only)return -EROFS;
    if(!node->filesystem->utimens)return -EOPNOTSUPP;
    return node->filesystem->utimens(node,atime,mtime,set_atime,set_mtime);
}
