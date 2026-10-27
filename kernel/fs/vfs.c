#include "vfs.h"
#include "lookup.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
#define NAS_VFS_MOUNT_LIMIT 16U
struct mount_slot {const struct nas_vfs_mount *mount;uint32_t open_refs;uint64_t device_id;};
static uint64_t next_device_id=8;
static struct mount_slot mounts[NAS_VFS_MOUNT_LIMIT];
static unsigned count;
static bool defaults_ready;
void nas_vfs_register_default_mounts(void);
static void ensure_defaults(void) {
    if (defaults_ready) return;
    defaults_ready = true;
    nas_vfs_register_default_mounts();
}
/* Canonical components keep longest-prefix lookup and busy-unmount coherent. */
static bool canonical_mountpoint(const char *path) {
    if(!path || path[0]!='/')return false;
    size_t n=strlen(path);
    if(n==1)return true;
    if(n<2 || n>=256 || path[n-1]=='/')return false;
    size_t start=1;
    for(size_t i=1;i<=n;i++) {
        if(i<n && path[i]!='/')continue;
        size_t length=i-start;
        if(!length || (length==1 && path[start]=='.') ||
           (length==2 && path[start]=='.' && path[start+1]=='.'))
            return false;
        start=i+1;
    }
    return true;
}
static bool mount_matches(const char *prefix, const char *path) {
    size_t n = strlen(prefix);
    if (n==1 && prefix[0]=='/') return path[0]=='/';
    return !strncmp(prefix,path,n) && (path[n]=='/' || path[n]==0);
}
int nas_vfs_register_mount(const struct nas_vfs_mount *mount) {
    ensure_defaults();
    if (!mount || !mount->type || !mount->type->lookup ||
        !mount->type->name || !canonical_mountpoint(mount->mountpoint))
        return -EINVAL;
    for (unsigned i=0;i<count;i++)
        if (!strcmp(mounts[i].mount->mountpoint,mount->mountpoint))return -EEXIST;
    if (count==NAS_VFS_MOUNT_LIMIT)return -ENOSPC;
    if(next_device_id==UINT64_MAX)return -ENOSPC;
    mounts[count++]=(struct mount_slot){.mount=mount,.device_id=next_device_id++};
    return 0;
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
            const struct nas_vfs_mount *m=mounts[i].mount;
            if (!mount_matches(m->mountpoint,absolute))continue;
            size_t n=strlen(m->mountpoint);
            if (!best || n>longest) {best=m;index=i;longest=n;}
        }
        if (!best)break;
        seen|=1U<<index;
        *out=(struct nas_fs_node){0};
        int rc=best->type->lookup(best,absolute,out);
        if(!rc) {out->filesystem=best->type;out->mount=best;return 0;}
        if(rc!=-ENOENT || !best->fallback_on_miss)return rc;
    }
    return -ENOENT;
}
int nas_vfs_readlink(const struct nas_fs_node *node,
                      const char *absolute,char target[256]) {
    /* Dynamic procfd entries have S_IFLNK metadata but deliberately do not
     * opt into generic namei-follow: opening a procfd requires FD semantics,
     * not blindly reopening printable targets such as pipe:[N]. */
    if (!node || !node->filesystem ||
        (node->mode&0170000U)!=0120000U ||
        !node->filesystem->readlink)return -EINVAL;
    return node->filesystem->readlink(node,absolute,target);
}
int64_t nas_vfs_read_at(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *dst,uint64_t length) {
    if(!node || !node->filesystem)return -EINVAL;
    if(node->is_dir)return -EISDIR;
    if(!dst && length)return -EINVAL;
    if(!node->filesystem->read_at)return -EOPNOTSUPP;
    return node->filesystem->read_at(node,offset,dst,length);
}
int nas_vfs_read_dir(const struct nas_fs_node *node,const char *path,
                     uint64_t cursor,struct nas_vfs_dirent *entries,
                     unsigned maximum) {
    if(!node || !node->filesystem || !path || !entries || !maximum)
        return -EINVAL;
    if(!node->is_dir)return -ENOTDIR;
    if(!node->filesystem->read_dir)return -EOPNOTSUPP;
    return node->filesystem->read_dir(node,path,cursor,entries,maximum);
}
bool nas_vfs_can_write(const struct nas_fs_node *node) {
    return node && node->filesystem && node->mount &&
           !node->filesystem->read_only && !node->mount->read_only;
}
/* A namespace operation always resolves the actual mounted PARENT first.
 * A new pathname has no inode to look up, so selecting by its own lookup
 * would accidentally fall through to an unrelated underlying mount. */
/* The provider sees a canonical path after following every parent symlink.
 * Otherwise mkdir('/tmp/alias/file') would dispatch to tmpfs but create
 * under the alias spelling rather than its true directory inode. */
static int vfs_parent(const char *path,struct nas_fs_node *parent,
                      char canonical[256]) {
    if(!path || path[0]!='/' || !parent || !canonical)return -EINVAL;
    size_t len=strlen(path);
    if(!len || len>=256)return -ENAMETOOLONG;
    if(len==1)return -EBUSY;
    if(path[len-1]=='/')return -ENOTDIR;
    size_t split=len;
    while(split>0 && path[split-1]!='/')--split;
    if(split==0)return -EINVAL;
    char dir[256],resolved[256];
    size_t size=split==1?1:split-1;
    memcpy(dir,path,size);dir[size]=0;
    int rc=nas_fs_lookup_node(dir,true,parent,resolved);
    if(rc)return rc;
    if(!parent->is_dir)return -ENOTDIR;
    size_t prefix=strlen(resolved),basename_len=len-split;
    bool needs_slash=prefix>1;
    if(prefix+(needs_slash?1U:0U)+basename_len>=256)
        return -ENAMETOOLONG;
    memcpy(canonical,resolved,prefix);
    if(needs_slash)canonical[prefix++]='/';
    memcpy(canonical+prefix,path+split,basename_len);
    canonical[prefix+basename_len]=0;
    return 0;
}
static int vfs_mutable_parent(const char *path,struct nas_fs_node *parent,
                              char canonical[256]) {
    int rc=vfs_parent(path,parent,canonical);
    if(rc)return rc;
    return nas_vfs_can_write(parent) ? 0 : -EROFS;
}
int nas_vfs_mkdir(const char *path,uint64_t mode) {
    if(path && !strcmp(path,"/"))return -EEXIST;
    struct nas_fs_node parent;char canonical[256];
    int rc=vfs_mutable_parent(path,&parent,canonical);
    if(rc)return rc;
    return parent.filesystem->mkdir ? parent.filesystem->mkdir(canonical,mode) : -EOPNOTSUPP;
}
int nas_vfs_unlink(const char *path,uint64_t flags) {
    struct nas_fs_node parent;char canonical[256];
    int rc=vfs_mutable_parent(path,&parent,canonical);
    if(rc)return rc;
    return parent.filesystem->unlink ? parent.filesystem->unlink(canonical,flags) : -EOPNOTSUPP;
}
int nas_vfs_rename(const char *oldpath,const char *newpath,uint64_t flags) {
    struct nas_fs_node old_parent,new_parent;
    char old_canonical[256],new_canonical[256];
    int rc=vfs_parent(oldpath,&old_parent,old_canonical);
    if(rc)return rc;
    rc=vfs_parent(newpath,&new_parent,new_canonical);
    if(rc)return rc;
    if(old_parent.mount!=new_parent.mount)return -EXDEV;
    if(!nas_vfs_can_write(&old_parent))return -EROFS;
    return old_parent.filesystem->rename ?
           old_parent.filesystem->rename(old_canonical,new_canonical,flags) : -EOPNOTSUPP;
}
int nas_vfs_link(const char *oldpath,const char *newpath) {
    char resolved[256],canonical[256];
    struct nas_fs_node source,parent;
    int rc=nas_fs_lookup_node(oldpath,false,&source,resolved);
    if(rc)return rc;
    rc=vfs_parent(newpath,&parent,canonical);
    if(rc)return rc;
    if(source.mount!=parent.mount)return -EXDEV;
    if(!nas_vfs_can_write(&parent))return -EROFS;
    return parent.filesystem->link ?
           parent.filesystem->link(resolved,canonical) : -EOPNOTSUPP;
}
int nas_vfs_symlink(const char *target,const char *path) {
    if(!target)return -EINVAL;
    struct nas_fs_node parent;char canonical[256];
    int rc=vfs_mutable_parent(path,&parent,canonical);
    if(rc)return rc;
    return parent.filesystem->symlink ?
           parent.filesystem->symlink(target,canonical) : -EOPNOTSUPP;
}
int nas_vfs_chmod(const struct nas_fs_node *node,uint64_t mode) {
    if(!node || !node->filesystem)return -EINVAL;
    if(node->filesystem->read_only || (node->mount && node->mount->read_only))return -EROFS;
    if(!node->filesystem->chmod)return -EOPNOTSUPP;
    return node->filesystem->chmod(node,mode);
}
int nas_vfs_utimens(const struct nas_fs_node *node,uint64_t atime,
                    uint64_t mtime,bool set_atime,bool set_mtime) {
    if(!node || !node->filesystem)return -EINVAL;
    if(node->filesystem->read_only || (node->mount && node->mount->read_only))return -EROFS;
    if(!node->filesystem->utimens)return -EOPNOTSUPP;
    return node->filesystem->utimens(node,atime,mtime,set_atime,set_mtime);
}

/* Mount table and File pins follow a single-CPU kernel policy. A registered
 * mount descriptor is owned by the caller; unregister rejects live Files and
 * descendants, preventing use-after-unmount for open descriptions. */
int nas_vfs_unregister_mount(const struct nas_vfs_mount *mount) {
    if(!mount)return -EINVAL;
    ensure_defaults();
    unsigned at=count;
    for(unsigned i=0;i<count;i++)if(mounts[i].mount==mount)at=i;
    if(at==count)return -ENOENT;
    if(mount->permanent || mounts[at].open_refs)return -EBUSY;
    size_t n=strlen(mount->mountpoint);
    for(unsigned i=0;i<count;i++) {
        if(i==at)continue;
        const char *p=mounts[i].mount->mountpoint;
        if((n==1 && mount->mountpoint[0]=='/') ||
           (!strncmp(mount->mountpoint,p,n) && p[n]=='/'))return -EBUSY;
    }
    for(unsigned i=at;i+1<count;i++)mounts[i]=mounts[i+1];
    mounts[--count]=(struct mount_slot){0};
    return 0;
}
int nas_vfs_node_pin(const struct nas_fs_node *node) {
    if(!node || !node->mount || node->filesystem!=node->mount->type)return -EINVAL;
    ensure_defaults();
    for(unsigned i=0;i<count;i++)if(mounts[i].mount==node->mount) {
        if(mounts[i].open_refs==UINT32_MAX)return -EMFILE;
        ++mounts[i].open_refs;return 0;
    }
    return -ENODEV;
}
int nas_vfs_node_unpin(const struct nas_fs_node *node) {
    if(!node || !node->mount)return -EINVAL;
    ensure_defaults();
    for(unsigned i=0;i<count;i++)if(mounts[i].mount==node->mount) {
        if(!mounts[i].open_refs)return -EINVAL;
        --mounts[i].open_refs;return 0;
    }
    return -ENODEV;
}

/* Stable during a mount's lifetime even if preceding mount table entries
 * are removed. Device ID 0 is always invalid; no kernel-address leak. */
uint64_t nas_vfs_node_device(const struct nas_fs_node *node) {
    if(!node || !node->mount)return 0;
    ensure_defaults();
    for(unsigned i=0;i<count;i++)
        if(mounts[i].mount==node->mount)return mounts[i].device_id;
    return 0;
}
