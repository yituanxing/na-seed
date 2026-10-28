#include "vfs.h"
#include "lookup.h"
#include "posix_locks.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stddef.h>
#define NAS_VFS_MOUNT_LIMIT 16U
#define NAS_VFS_LIVE_INODES 256U
/* A bounded, refcounted inode-size identity table. Owned File and cwd
 * mount pins acquire an inode lease; dup/fork share the owning File.
 * No File/Task syscall kind is embedded in VFS, and no stale pathname is
 * reopened after rename. VFS is single-CPU, matching mount ref policy. */
struct inode_size_lease {
    const struct nas_vfs_mount *mount;
    uint64_t ino;
    const void *backend_inode; /* only pointer identity, never VFS-owned */
    uint64_t size;
    uint32_t refs;
    struct nas_posix_locks posix_locks; /* process owner PID, not OFD slot */
};
static struct inode_size_lease live_inodes[NAS_VFS_LIVE_INODES];
/* Shared validation is defined after mount registry/default initialization,
 * but already used by stat/inode operations above that definition. */
static int vfs_node_status(const struct nas_fs_node *node);
/* flock is keyed by two distinct owners: (mount,inode incarnation)
 * for conflict detection, and the shared File object pointer for lifetime.
 * No FD number, task and backend-specific inode struct appears here. */
#define NAS_VFS_FLOCK_LIMIT 128U
struct vfs_flock_entry {
    const struct nas_vfs_mount *mount;
    uint64_t ino;
    const void *backend_inode;
    const void *file_owner;
    bool exclusive;
};
static struct vfs_flock_entry vfs_flocks[NAS_VFS_FLOCK_LIMIT];
bool nas_vfs_inode_is_pinned(const void *backend_inode) {
    if(!backend_inode)return false;
    for(unsigned i=0;i<NAS_VFS_LIVE_INODES;i++)
        if(live_inodes[i].refs &&
           live_inodes[i].backend_inode==backend_inode)return true;
    return false;
}
static struct inode_size_lease *inode_lease(const struct nas_fs_node *node) {
    if(!node || !node->mount)return NULL;
    for(unsigned i=0;i<NAS_VFS_LIVE_INODES;i++)
        if(live_inodes[i].refs &&
           live_inodes[i].mount==node->mount &&
           live_inodes[i].ino==node->ino)return &live_inodes[i];
    return NULL;
}
/* An inode lease is scoped to its mount AND backend incarnation. A stale
 * reused inode number cannot steal a newer File's size or owner reference. */
static bool lease_conflicts(const struct inode_size_lease *lease,
                            const struct nas_fs_node *node) {
    return lease && lease->backend_inode!=node->inode;
}
struct nas_posix_locks *nas_vfs_posix_locks(const struct nas_fs_node *node) {
    if(vfs_node_status(node))return NULL;
    struct inode_size_lease *lease=inode_lease(node);
    return lease && !lease_conflicts(lease,node) ?
           &lease->posix_locks : NULL;
}
void nas_vfs_release_posix_locks(const struct nas_fs_node *node,uint64_t pid) {
    struct nas_posix_locks *locks=nas_vfs_posix_locks(node);
    if(locks && pid)nas_locks_release(locks,pid);
}
static bool flock_matches_node(const struct vfs_flock_entry *entry,
                               const struct nas_fs_node *node) {
    return entry->file_owner && entry->mount==node->mount &&
           entry->ino==node->ino && entry->backend_inode==node->inode;
}
int nas_vfs_flock(const struct nas_fs_node *node,const void *owner,
                  unsigned operation) {
    if(!node || !owner || !node->mount ||
       !node->filesystem || node->is_dir)return -EBADF;
    int status=vfs_node_status(node);
    if(status)return status;
    struct inode_size_lease *lease=inode_lease(node);
    if(!lease || lease_conflicts(lease,node))return -ESTALE;
    unsigned kind=operation&~4U; /* LOCK_NB */
    if((operation&~(1U|2U|4U|8U)) ||
       (kind!=1U && kind!=2U && kind!=8U))return -EINVAL;
    struct vfs_flock_entry *mine=NULL,*empty=NULL;
    for(unsigned i=0;i<NAS_VFS_FLOCK_LIMIT;i++) {
        struct vfs_flock_entry *entry=&vfs_flocks[i];
        if(!entry->file_owner) {
            if(!empty)empty=entry;
            continue;
        }
        if(entry->file_owner==owner) {
            mine=entry;
            continue;
        }
        if(kind!=8U && flock_matches_node(entry,node) &&
           (kind==2U || entry->exclusive))return -EWOULDBLOCK;
    }
    if(mine)*mine=(struct vfs_flock_entry){0};
    if(kind==8U)return 0;
    struct vfs_flock_entry *target=mine?mine:empty;
    if(!target)return -ENOLCK;
    *target=(struct vfs_flock_entry){
        .mount=node->mount,.ino=node->ino,.backend_inode=node->inode,
        .file_owner=owner,.exclusive=kind==2U
    };
    return 0;
}
void nas_vfs_release_flock(const void *owner) {
    if(!owner)return;
    for(unsigned i=0;i<NAS_VFS_FLOCK_LIMIT;i++)
        if(vfs_flocks[i].file_owner==owner)
            vfs_flocks[i]=(struct vfs_flock_entry){0};
}
uint64_t nas_vfs_node_size(const struct nas_fs_node *node) {
    if(!node)return 0;
    struct inode_size_lease *lease=inode_lease(node);
    return lease && !lease_conflicts(lease,node) ? lease->size : node->size;
}
int nas_vfs_getattr(const struct nas_fs_node *node,struct nas_fs_node *out) {
    if(!out)return -EINVAL;
    int status=vfs_node_status(node);
    if(status)return status;
    struct inode_size_lease *lease=inode_lease(node);
    if(lease_conflicts(lease,node))return -ESTALE;
    *out=*node;
    if(node->filesystem->getattr) {
        int rc=node->filesystem->getattr(node,out);
        if(rc)return rc;
        if(out->filesystem!=node->filesystem || out->mount!=node->mount ||
           out->inode!=node->inode || out->ino!=node->ino ||
           out->mount_id!=node->mount_id ||
           out->is_dir!=node->is_dir)return -ESTALE;
    }
    if(lease)out->size=lease->size;
    return 0;
}
static void publish_inode_size(const struct nas_fs_node *node,uint64_t size) {
    struct inode_size_lease *lease=inode_lease(node);
    if(lease && !lease_conflicts(lease,node))lease->size=size;
}
/* A mount is an owned attachment to its *covered parent inode*, not
 * merely a path prefix. The parent mount retains one child reference until
 * detach, independent of the File/cwd inode leases. This is the first
 * counted mount-tree seam (Linux vfsmount, NaOS mnt_parent/mountpoint). */
struct mount_slot {
    const struct nas_vfs_mount *mount;
    const struct nas_vfs_mount *parent_mount;
    const void *parent_backend_inode;
    uint64_t parent_ino;
    uint32_t open_refs,child_refs;
    uint64_t device_id;
    bool parent_anchored;
    /* A child attachment must OWN its covered dentry inode and parent
     * directory identity. A bare (ino,pointer) tuple cannot protect a
     * tmpfs parent from final unlink/reclaim and reuse. These are ordinary
     * VFS leases on the parent filesystem, never on the child mount. */
    bool parent_pinned,covered_pinned;
    struct nas_fs_node parent_node,covered_node;
};
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
/* Every inode data/control operation validates the SAME live mount and
 * incarnation that File/open, stat and the refcounted lease table own.
 * A borrowed snapshot from a detached mount must never call into a
 * destroyed device adapter. A recycled ino with a different backend
 * identity must not reach any provider before the final old File closes. */
static int vfs_node_status(const struct nas_fs_node *node) {
    if(!node || !node->filesystem || !node->mount)return -EINVAL;
    ensure_defaults();
    /* Never dereference node->mount before finding its registered, live
     * attachment. An old borrowed snapshot may retain a freed descriptor
     * address after umount. */
    for(unsigned i=0;i<count;i++)
        if(mounts[i].mount==node->mount) {
            if(node->mount_id!=mounts[i].device_id)return -ESTALE;
            if(node->filesystem!=node->mount->type)return -EINVAL;
            if(lease_conflicts(inode_lease(node),node))return -ESTALE;
            return node->filesystem->validate_inode ?
                   node->filesystem->validate_inode(node) : 0;
        }
    return -ENODEV;
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
    if (!mount || !mount->type || !mount->type->root ||
        !mount->type->lookup_child || !mount->type->name ||
        !canonical_mountpoint(mount->mountpoint))
        return -EINVAL;
    for (unsigned i=0;i<count;i++)
        if (!strcmp(mounts[i].mount->mountpoint,mount->mountpoint))return -EEXIST;
    /* Bind the attachment to the real parent inode. String prefix mount
     * selection alone is insufficient: a symlink alias or moved parent
     * could otherwise silently redirect a mounted subtree. */
    struct nas_fs_node parent={0},covered={0};
    bool has_parent=false,has_covered=false;
    unsigned parent_slot=count;
    /* Validate both the parent and the covered mountpoint inode before
     * adding the new mount. The prefix selector must not mask a regular
     * file at the mount target, nor invent intermediate directories.
     * A missing / itself is the ONLY direct-ELF bootstrap exception. */
    if(strcmp(mount->mountpoint,"/")) {
        char parent_path[256],resolved[256];
        size_t len=strlen(mount->mountpoint);
        memcpy(parent_path,mount->mountpoint,len+1);
        while(len>1 && parent_path[len-1]!='/')len--;
        if(len>1)len--;
        parent_path[len]=0;
        int parent_rc=nas_fs_lookup_node(parent_path,true,&parent,resolved);
        if(parent_rc==-ENOENT) {
            /* CPIO-less direct-ELF boots have no root dentry but still
             * attach a supplied disk at /mnt. Do not extend this exception
             * to arbitrary missing ancestor components such as /x/y. */
            if(strcmp(parent_path,"/"))return -ENOENT;
            struct nas_fs_node root;
            int root_rc=nas_vfs_lookup("/",&root);
            if(root_rc!=-ENOENT)return root_rc?root_rc:-ENOENT;
        } else {
            if(parent_rc)return parent_rc;
            if(!parent.is_dir)return -ENOTDIR;
            /* The mountpoint is already a canonical path. Linux resolves
             * mounting through a symlink before attachment; until a fully
             * owned dentry API exists, refuse an ambiguous alias instead
             * of creating a mount at the wrong name. */
            if(strcmp(parent_path,resolved))return -EINVAL;
            for(unsigned i=0;i<count;i++)
                if(mounts[i].mount==parent.mount) {
                    parent_slot=i;break;
                }
            if(parent_slot==count)return -ENODEV;
            if(mounts[parent_slot].child_refs==UINT32_MAX)
                return -EMFILE;
            has_parent=true;
        }
        int covered_rc=nas_fs_lookup_node(mount->mountpoint,true,
                                           &covered,resolved);
        if(!covered_rc) {
            if(!covered.is_dir)return -ENOTDIR;
            /* The target itself can be a symlink, not just its parent.
             * Reject a spelling that resolves elsewhere until a counted
             * dentry attachment can represent Linux's actual mountpoint. */
            if(strcmp(resolved,mount->mountpoint))return -EINVAL;
            has_covered=true;
        }
        if(covered_rc && covered_rc!=-ENOENT)return covered_rc;
    }
    if (count==NAS_VFS_MOUNT_LIMIT)return -ENOSPC;
    if(next_device_id==UINT64_MAX)return -ENOSPC;
    struct mount_slot attached={.mount=mount};
    if(has_parent) {
        /* First acquire all backing references, then publish attachment.
         * Failure must leave the parent inode, child count and mount
         * registry unchanged, even with the inode-lease table exhausted. */
        int rc=nas_vfs_node_pin(&parent);
        if(rc)return rc;
        attached.parent_pinned=true;
        attached.parent_node=parent;
        if(has_covered) {
            rc=nas_vfs_node_pin(&covered);
            if(rc) {
                (void)nas_vfs_node_unpin(&parent);
                return rc;
            }
            attached.covered_pinned=true;
            attached.covered_node=covered;
        }
        attached.parent_anchored=true;
        attached.parent_mount=parent.mount;
        attached.parent_ino=parent.ino;
        attached.parent_backend_inode=parent.inode;
        ++mounts[parent_slot].child_refs;
    }
    attached.device_id=next_device_id++;
    mounts[count++]=attached;
    return 0;
}
/* One component resolver shared by all filesystems. VFS alone selects the
 * mount, strips its mountpoint and handles the legacy overlay fallback.
 * A provider only receives (parent inode, child basename), never /mnt/... .
 * This is an interim prefix mount selector until dentry-owned mounts land. */
int nas_vfs_lookup(const char *absolute,struct nas_fs_node *out) {
    if(!absolute || !out || absolute[0]!='/')return -EINVAL;
    size_t path_len=strlen(absolute);
    if(path_len>=256)return -ENAMETOOLONG;
    ensure_defaults();
    unsigned seen=0;
    while(seen < ((1U<<count)-1U)) {
        const struct nas_vfs_mount *best=NULL;
        unsigned selected=0;
        size_t longest=0;
        for(unsigned i=0;i<count;i++) {
            if(seen&(1U<<i))continue;
            const struct nas_vfs_mount *m=mounts[i].mount;
            if(!mount_matches(m->mountpoint,absolute))continue;
            size_t n=strlen(m->mountpoint);
            if(!best || n>longest) {best=m;selected=i;longest=n;}
        }
        if(!best)break;
        seen|=1U<<selected;
        struct nas_fs_node current={0};
        int rc=best->type->root(best,&current);
        if(!rc) {
            current.filesystem=best->type;
            current.mount=best;
            current.mount_id=mounts[selected].device_id;
            const char *rest=absolute+longest;
            if(longest==1)rest=absolute+1;
            while(*rest=='/')rest++;
            while(*rest) {
                if(!current.is_dir){rc=-ENOTDIR;break;}
                char name[256];
                unsigned n=0;
                while(rest[n] && rest[n]!='/') {
                    if(n==255)return -ENAMETOOLONG;
                    name[n]=rest[n];n++;
                }
                name[n]=0;
                /* The shared namei layer handles dot, dotdot and symlinks.
                 * Never present these special components as filesystem names. */
                if((n==1 && name[0]=='.') ||
                   (n==2 && name[0]=='.' && name[1]=='.'))
                    return -EINVAL;
                struct nas_fs_node next={0};
                rc=best->type->lookup_child(&current,name,&next);
                if(rc)break;
                next.mount=best;
                next.filesystem=best->type;
                next.mount_id=mounts[selected].device_id;
                current=next;
                rest+=n;
                while(*rest=='/')rest++;
            }
        }
        if(!rc) {*out=current;return 0;}
        if(rc!=-ENOENT || !best->fallback_on_miss)return rc;
    }
    return -ENOENT;
}

int nas_vfs_lookup_step(const struct nas_fs_node *parent,const char *absolute,
                         const char *name,struct nas_fs_node *out) {
    if(!absolute || !name || !name[0] || !out || absolute[0]!='/' ||
       strchr(name,'/') ||
       (name[0]=='.' && (!name[1] ||
                         (name[1]=='.' && !name[2]))))
        return -EINVAL;
    ensure_defaults();
    /* Even a borrowed parent from a stale directory FD cannot cross an
     * attachment or route new lookups into a no-longer-mounted backend. */
    if(parent) {
        int status=vfs_node_status(parent);
        if(status)return status;
        if(!parent->is_dir)return -ENOTDIR;
    }
    /* Cross a mount only at the EXACT component-aligned mountpoint.
     * An open parent need not belong to the target mount. */
    for(unsigned i=0;i<count;i++) {
        const struct nas_vfs_mount *mnt=mounts[i].mount;
        if(strcmp(mnt->mountpoint,absolute))continue;
        const struct mount_slot *slot=&mounts[i];
        if(slot->parent_anchored) {
            /* Parent-mount identity is part of the attachment. Inode
             * number alone is not enough: detect backend inode reuse. */
            if(!parent || parent->mount!=slot->parent_mount ||
               parent->ino!=slot->parent_ino ||
               parent->inode!=slot->parent_backend_inode)
                return -ESTALE;
        } else if(parent && strcmp(mnt->mountpoint,"/") &&
                  parent->mount && !parent->is_dir) {
            return -ENOTDIR;
        }
        *out=(struct nas_fs_node){0};
        int rc=mnt->type->root(mnt,out);
        if(!rc) {
            out->filesystem=mnt->type;
            out->mount=mnt;
            out->mount_id=slot->device_id;
        }
        return rc;
    }
    if(!parent || !parent->mount || !parent->filesystem)
        return -ENOENT;
    if(!parent->is_dir)return -ENOTDIR;
    *out=(struct nas_fs_node){0};
    int rc=parent->filesystem->lookup_child(parent,name,out);
    if(!rc) {
        out->mount=parent->mount;
        out->filesystem=parent->filesystem;
        out->mount_id=parent->mount_id;
        return 0;
    }
    if(rc==-ENOENT && parent->mount->fallback_on_miss)
        return nas_vfs_lookup(absolute,out);
    return rc;
}
int nas_vfs_readlink(const struct nas_fs_node *node,char target[256]) {
    /* Dynamic procfd entries have S_IFLNK metadata but deliberately do not
     * opt into generic namei-follow: opening a procfd requires FD semantics,
     * not blindly reopening printable targets such as pipe:[N]. */
    if (!node || !node->filesystem ||
        (node->mode&0170000U)!=0120000U ||
        !node->filesystem->readlink)return -EINVAL;
    if(!target)return -EINVAL;
    int status=vfs_node_status(node);
    if(status)return status;
    return node->filesystem->readlink(node,target);
}
int64_t nas_vfs_read_at(const struct nas_fs_node *node,uint64_t offset,
                        uint8_t *dst,uint64_t length) {
    int status=vfs_node_status(node);
    if(status)return status;
    if(node->is_dir)return -EISDIR;
    if(!dst && length)return -EINVAL;
    if(!node->filesystem->read_at)return -EOPNOTSUPP;
    int64_t got=node->filesystem->read_at(node,offset,dst,length);
    return got>=0 && (uint64_t)got>length ? -EIO : got;
}
int nas_vfs_directory_path(const struct nas_fs_node *node,
                           const char *origin_hint,char absolute[256]) {
    if(!node || !node->filesystem || !node->mount || !absolute)
        return -EINVAL;
    if(!node->is_dir)return -ENOTDIR;
    int status=vfs_node_status(node);
    if(status)return status;
    /* An inode number of zero cannot identify an owned open directory.
     * Refuse path-based operations rather than accidentally authorizing
     * an unrelated re-used pathname under the same mount. */
    if(!node->ino)return -ESTALE;
    char selected[256];
    int rc=-EOPNOTSUPP;
    if(node->filesystem->directory_path) {
        rc=node->filesystem->directory_path(node,selected);
        if(rc && rc!=-EOPNOTSUPP)return rc;
    }
    if(rc==-EOPNOTSUPP) {
        if(!origin_hint || origin_hint[0]!='/')return -ESTALE;
        size_t len=strlen(origin_hint);
        if(len>=sizeof(selected))return -ENAMETOOLONG;
        memcpy(selected,origin_hint,len+1);
    }
    if(selected[0]!='/')return -EIO;
    struct nas_fs_node current;
    char canonical[256];
    rc=nas_fs_lookup_node(selected,true,&current,canonical);
    /* This is namespace-to-identity validation, not an attempt to
     * reopen the old path. A recycled dentry must NEVER become a way to
     * perform openat/unlinkat relative to a different directory. */
    if(rc== -ENAMETOOLONG)return rc;
    if(rc || current.mount!=node->mount ||
       current.mount_id!=node->mount_id || current.ino!=node->ino || current.inode!=node->inode ||
       !current.is_dir)return -ESTALE;
    size_t len=strlen(canonical);
    memcpy(absolute,canonical,len+1);
    return 0;
}
int nas_vfs_read_dir(const struct nas_fs_node *node,
                     uint64_t cursor,struct nas_vfs_dirent *entries,
                     unsigned maximum) {
    if(!node || !node->filesystem || !node->mount ||
       !entries || !maximum)
        return -EINVAL;
    if(!node->is_dir)return -ENOTDIR;
    int status=vfs_node_status(node);
    if(status)return status;
    if(!node->filesystem->read_dir)return -EOPNOTSUPP;
    int got=node->filesystem->read_dir(node,cursor,entries,maximum);
    if(got<0)return got;
    if((unsigned)got>maximum)return -EIO;
    for(int i=0;i<got;i++) {
        unsigned length=entries[i].name_len;
        if(!entries[i].ino || !length || length>255 ||
           entries[i].name[length])return -EIO;
        for(unsigned n=0;n<length;n++)
            if(!entries[i].name[n] || entries[i].name[n]=='/')
                return -EIO;
    }
    return got;
}
bool nas_vfs_can_write(const struct nas_fs_node *node) {
    return !vfs_node_status(node) &&
           !node->filesystem->read_only && !node->mount->read_only;
}
/* A namespace operation always resolves the actual mounted PARENT first.
 * A new pathname has no inode to look up, so selecting by its own lookup
 * would accidentally fall through to an unrelated underlying mount. */
/* The provider sees a canonical path after following every parent symlink.
 * Otherwise mkdir('/tmp/alias/file') would dispatch to tmpfs but create
 * under the alias spelling rather than its true directory inode. */
/* A namespace mutation must not remove, rename or replace an active
 * mountpoint. Even an empty mounted directory is BUSY in Linux; choosing
 * the underlying provider by its parent would otherwise bypass the VFS
 * mount tree and corrupt a different filesystem's directory. */
static bool active_mountpoint(const char *path) {
    if(!path)return false;
    ensure_defaults();
    for(unsigned i=0;i<count;i++) {
        /* A mounted subtree pins its ancestor against rename/rmdir as
         * well: removing /a while /a/b is mounted would orphan /a/b.
         * mount_matches compares whole path components, not raw prefixes. */
        if(mount_matches(path,mounts[i].mount->mountpoint))return true;
    }
    return false;
}
static int vfs_mutable_parent(const char *path,struct nas_fs_node *parent,
                              char canonical[256]) {
    int rc=nas_fs_lookup_parent(path,parent,canonical);
    if(rc)return rc;
    return nas_vfs_can_write(parent) ? 0 : -EROFS;
}
/* vfs_parent resolved and validated the real directory inode. The final
 * basename is not a pathname: this is the only namespace callback input. */
static const char *vfs_leaf(const char *canonical) {
    const char *last=strrchr(canonical,'/');
    return last ? last+1 : canonical;
}
/* Linux do_filp_open and NaOS vfs_open belong on the common VFS side of
 * the syscall boundary. The Linux ABI adapter supplies decoded options,
 * and no provider-specific inode or open File subclass leaks into this API.
 * A successful open owns exactly one inode lease and mount reference. */
int nas_vfs_open_inode(const char *path,
                       const struct nas_vfs_open_options *options,
                       struct nas_fs_node *owned,char canonical[256]) {
    if(!path || path[0]!='/' || !options || !owned || !canonical)
        return -EINVAL;
    if(options->access>2)return -EINVAL;
    struct nas_fs_node node={0};
    /* O_CREAT|O_EXCL must examine a final symlink itself, not follow it
     * to a missing destination and accidentally create another inode. */
    bool follow=options->follow_final &&
                !(options->create && options->exclusive);
    int rc=nas_fs_lookup_node(path,follow,&node,canonical);
    bool newly_created=false;
    if(rc==-ENOENT && options->create) {
        /* In single-CPU VFS a new inode necessarily needs a fresh lease.
         * Check capacity BEFORE creating a visible name: ENFILE must not
         * leave a file behind when the subsequent File cannot own it.
         * Existing files are handled by the exact-identity pin below. */
        bool free_inode_slot=false;
        for(unsigned i=0;i<NAS_VFS_LIVE_INODES;i++)
            if(!live_inodes[i].refs) {free_inode_slot=true;break;}
        if(!free_inode_slot)return -ENFILE;
        /* Never write a regular inode when the caller requires a dir. */
        if(options->directory_only)return -ENOTDIR;
        if(options->truncate && !options->path_only && !options->access)
            return -EINVAL;
        rc=nas_vfs_create(path,options->create_mode);
        if(rc)return rc;
        rc=nas_fs_lookup_node(path,follow,&node,canonical);
        if(rc)return rc;
        newly_created=true;
    } else if(rc)return rc;
    if(!newly_created && options->create && options->exclusive)
        return -EEXIST;
    if(options->directory_only && !node.is_dir)return -ENOTDIR;
    if(node.is_symlink && !follow && !options->path_only)
        return -ELOOP;
    if((node.mode&0170000U)==0140000U && !options->path_only)
        return -ENXIO; /* UNIX sockets are not byte files */
    if(node.is_dir && (options->access || options->truncate))
        return -EISDIR;
    if(!options->path_only && !node.is_dir &&
       (options->access || options->truncate) &&
       !nas_vfs_can_write(&node))return -EROFS;
    if(options->truncate && !options->access && !options->path_only)
        return -EINVAL;
    /* Acquire inode ownership BEFORE a potentially destructive truncate:
     * stale incarnation, ENFILE or mount limits must not erase data. */
    rc=nas_vfs_node_pin(&node);
    if(rc)return rc;
    if(options->truncate && !options->path_only) {
        rc=nas_vfs_resize(&node,0);
        if(rc) {
            (void)nas_vfs_node_unpin(&node);
            return rc;
        }
        node.size=0;
    }
    *owned=node; /* transfer the unique pin to its shared File owner */
    return 0;
}
int nas_vfs_create(const char *path,uint64_t mode) {
    if(!path || path[0]!='/')return -EINVAL;
    struct nas_fs_node existing;
    char resolved[256];
    int rc=nas_fs_lookup_node(path,false,&existing,resolved);
    if(!rc)return -EEXIST;
    if(rc!=-ENOENT)return rc;
    struct nas_fs_node parent;char canonical[256];
    rc=vfs_mutable_parent(path,&parent,canonical);
    if(rc)return rc;
    return parent.filesystem->create ?
           parent.filesystem->create(&parent,vfs_leaf(canonical),mode) : -EOPNOTSUPP;
}
int64_t nas_vfs_write_at(const struct nas_fs_node *node,uint64_t offset,
                         const uint8_t *src,uint64_t length) {
    int status=vfs_node_status(node);
    if(status)return status;
    if(!nas_vfs_can_write(node))return -EROFS;
    if(node->is_dir)return -EISDIR;
    if(!src && length)return -EFAULT;
    if(offset>INT64_MAX || length>INT64_MAX-offset)return -EFBIG;
    if(!node->filesystem->write_at)return -EOPNOTSUPP;
    int64_t written=node->filesystem->write_at(node,offset,src,length);
    if(written>0 && (uint64_t)written>length)return -EIO;
    if(written>0 && offset+(uint64_t)written>nas_vfs_node_size(node))
        publish_inode_size(node,offset+(uint64_t)written);
    return written;
}
int nas_vfs_resize(const struct nas_fs_node *node,uint64_t length) {
    int status=vfs_node_status(node);
    if(status)return status;
    if(node->is_dir)return -EISDIR;
    if(length>INT64_MAX)return -EFBIG;
    if(!nas_vfs_can_write(node))return -EROFS;
    if(!node->filesystem->resize)return -EOPNOTSUPP;
    int rc=node->filesystem->resize(node,length);
    if(!rc)publish_inode_size(node,length);
    return rc;
}
int nas_vfs_sync(const struct nas_fs_node *node) {
    int status=vfs_node_status(node);
    if(status)return status;
    if(!node->filesystem->sync)return node->filesystem->read_only ? 0 : -EOPNOTSUPP;
    return node->filesystem->sync(node);
}
int nas_vfs_mkdir(const char *path,uint64_t mode) {
    if(!path || path[0]!='/')return -EINVAL;
    if(!strcmp(path,"/"))return -EEXIST;
    /* POSIX mkdir(existing) is EEXIST, even for a read-only mountpoint.
     * Inspect namei before deciding whether the new parent's mount can
     * accept a mutation; this is essential for BusyBox mkdir -p. */
    struct nas_fs_node existing;
    char existing_path[256];
    int rc=nas_fs_lookup_node(path,false,&existing,existing_path);
    if(!rc)return -EEXIST;
    if(rc!=-ENOENT)return rc;
    struct nas_fs_node parent;char canonical[256];
    rc=vfs_mutable_parent(path,&parent,canonical);
    if(rc)return rc;
    return parent.filesystem->mkdir ?
           parent.filesystem->mkdir(&parent,vfs_leaf(canonical),mode) : -EOPNOTSUPP;
}
int nas_vfs_unlink(const char *path,uint64_t flags) {
    if(flags&~0x200ULL)return -EINVAL; /* Linux AT_REMOVEDIR only */
    struct nas_fs_node parent;char canonical[256];
    /* Mount-tree identity takes precedence over the parent filesystem's
     * writable flag: rmdir(/mnt) is EBUSY even if /mnt lives below an
     * immutable bootfs directory. Read-only policy applies afterward. */
    int rc=nas_fs_lookup_parent(path,&parent,canonical);
    if(rc)return rc;
    if(active_mountpoint(canonical))return -EBUSY;
    if(!nas_vfs_can_write(&parent))return -EROFS;
    /* Upstream lwext4 fremove() immediately truncates/frees a last-link
     * inode, even when a shared File owns a live mount+inode pin. Until an
     * inode-orphan lifetime protocol exists, fail closed. EBUSY is an
     * acknowledged temporary deviation from Linux unlink-open, not a claim
     * of POSIX compliance. tmpfs explicitly opts into safe unlink-open. */
    if(!parent.filesystem->unlink_open_safe) {
        struct nas_fs_node target;
        int looked_up=nas_vfs_lookup(canonical,&target);
        /* rmdir(open directory) is just as unsafe as unlink(open file).
         * Directory st_nlink normally counts '.' and a parent's entry,
         * so the last-file-link threshold cannot be used for directories. */
        if(!looked_up && target.mount==parent.mount && target.ino &&
           inode_lease(&target) &&
           ((flags&0x200ULL) || target.nlink<=1))
            return -EBUSY;
        if(looked_up && looked_up!=-ENOENT)return looked_up;
    }
    return parent.filesystem->unlink ?
           parent.filesystem->unlink(&parent,vfs_leaf(canonical),flags) : -EOPNOTSUPP;
}
int nas_vfs_rename(const char *oldpath,const char *newpath,uint64_t flags) {
    struct nas_fs_node old_parent,new_parent;
    char old_canonical[256],new_canonical[256];
    int rc=nas_fs_lookup_parent(oldpath,&old_parent,old_canonical);
    if(rc)return rc;
    rc=nas_fs_lookup_parent(newpath,&new_parent,new_canonical);
    if(rc)return rc;
    if(active_mountpoint(old_canonical) || active_mountpoint(new_canonical))
        return -EBUSY;
    if(old_parent.mount!=new_parent.mount)return -EXDEV;
    if(!nas_vfs_can_write(&old_parent))return -EROFS;
    if(!old_parent.filesystem->rename)return -EOPNOTSUPP;
    /* POSIX rename(replace) must not invalidate an already open target.
     * In lwext4 the replace path may unlink/free the victim inode while
     * VFS Files still pin it. Default to fail-closed until the provider
     * offers proper orphan/deferred reclaim (tmpfs already does). */
    if(!new_parent.filesystem->rename_replace_open_safe &&
       strcmp(old_canonical,new_canonical)) {
        struct nas_fs_node victim;
        int found=nas_vfs_lookup(new_canonical,&victim);
        if(!found && victim.mount==new_parent.mount && victim.ino &&
           inode_lease(&victim)) {
            /* A hardlink alias of source is the same inode, not a victim. */
            struct nas_fs_node source;
            int src=nas_vfs_lookup(old_canonical,&source);
            if(src)return src;
            if(source.mount!=victim.mount || source.ino!=victim.ino)
                return -EBUSY;
        }
        if(found && found!=-ENOENT)return found;
    }
    return old_parent.filesystem->rename ?
           old_parent.filesystem->rename(&old_parent,vfs_leaf(old_canonical),
                                          &new_parent,vfs_leaf(new_canonical),
                                          flags) : -EOPNOTSUPP;
}
int nas_vfs_link(const char *oldpath,const char *newpath) {
    char resolved[256],canonical[256];
    struct nas_fs_node source,parent;
    int rc=nas_fs_lookup_node(oldpath,false,&source,resolved);
    if(rc)return rc;
    /* Linux hardlinks cannot target directories, regardless of backend.
     * Don't defer directory link restrictions to an arbitrary filesystem. */
    if(source.is_dir)return -EPERM;
    rc=nas_fs_lookup_parent(newpath,&parent,canonical);
    if(rc)return rc;
    if(source.mount!=parent.mount)return -EXDEV;
    if(!nas_vfs_can_write(&parent))return -EROFS;
    struct nas_fs_node old_parent;
    char old_canonical[256];
    rc=nas_fs_lookup_parent(resolved,&old_parent,old_canonical);
    if(rc)return rc;
    if(old_parent.mount!=parent.mount)return -EXDEV;
    return parent.filesystem->link ?
           parent.filesystem->link(&old_parent,vfs_leaf(old_canonical),
                                     &parent,vfs_leaf(canonical)) : -EOPNOTSUPP;
}
int nas_vfs_symlink(const char *target,const char *path) {
    if(!target)return -EINVAL;
    struct nas_fs_node parent;char canonical[256];
    int rc=vfs_mutable_parent(path,&parent,canonical);
    if(rc)return rc;
    return parent.filesystem->symlink ?
           parent.filesystem->symlink(&parent,vfs_leaf(canonical),target) : -EOPNOTSUPP;
}
int nas_vfs_chmod(const struct nas_fs_node *node,uint64_t mode) {
    int status=vfs_node_status(node);
    if(status)return status;
    if(node->filesystem->read_only || (node->mount && node->mount->read_only))return -EROFS;
    if(!node->filesystem->chmod)return -EOPNOTSUPP;
    return node->filesystem->chmod(node,mode);
}
int nas_vfs_utimens(const struct nas_fs_node *node,uint64_t atime,
                    uint64_t mtime,bool set_atime,bool set_mtime) {
    int status=vfs_node_status(node);
    if(status)return status;
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
    if(mount->permanent || mounts[at].open_refs ||
       mounts[at].child_refs)return -EBUSY;
    size_t n=strlen(mount->mountpoint);
    for(unsigned i=0;i<count;i++) {
        if(i==at)continue;
        const char *p=mounts[i].mount->mountpoint;
        if((n==1 && mount->mountpoint[0]=='/') ||
           (!strncmp(mount->mountpoint,p,n) && p[n]=='/'))return -EBUSY;
    }
    /* Detach in reverse ownership order. The parent mount remains attached
     * until its last child pin has been released. A failed precondition
     * must not release even one backing inode before reporting EIO. */
    unsigned parent_at=count;
    if(mounts[at].parent_anchored) {
        for(unsigned i=0;i<count;i++)
            if(mounts[i].mount==mounts[at].parent_mount) {
                parent_at=i;break;
            }
        if(parent_at==count || !mounts[parent_at].child_refs ||
           !mounts[at].parent_pinned)return -EIO;
        if(!inode_lease(&mounts[at].parent_node) ||
           (mounts[at].covered_pinned &&
            !inode_lease(&mounts[at].covered_node)))return -EIO;
    }
    if(mounts[at].covered_pinned &&
       nas_vfs_node_unpin(&mounts[at].covered_node))return -EIO;
    if(mounts[at].parent_pinned &&
       nas_vfs_node_unpin(&mounts[at].parent_node))return -EIO;
    if(parent_at<count)--mounts[parent_at].child_refs;
    for(unsigned i=at;i+1<count;i++)mounts[i]=mounts[i+1];
    mounts[--count]=(struct mount_slot){0};
    return 0;
}
int nas_vfs_node_pin(const struct nas_fs_node *node) {
    if(!node || !node->filesystem || !node->mount ||
       (!node->ino && !node->inode))return -EINVAL;
    int status=vfs_node_status(node);
    if(status)return status;
    for(unsigned i=0;i<count;i++)if(mounts[i].mount==node->mount) {
        if(mounts[i].open_refs==UINT32_MAX)return -EMFILE;
        struct inode_size_lease *lease=inode_lease(node);
        if(lease) {
            if(lease_conflicts(lease,node))return -ESTALE;
            if(lease->refs==UINT32_MAX)return -EMFILE;
            ++lease->refs;
        } else {
            for(unsigned n=0;n<NAS_VFS_LIVE_INODES;n++)
                if(!live_inodes[n].refs) {
                    lease=&live_inodes[n];
                    *lease=(struct inode_size_lease){
                        .mount=node->mount,.ino=node->ino,
                        .backend_inode=node->inode,.size=node->size,.refs=1
                    };
                    break;
                }
            if(!lease)return -ENFILE;
        }
        ++mounts[i].open_refs;return 0;
    }
    return -ENODEV;
}
int nas_vfs_node_unpin(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->filesystem)return -EINVAL;
    ensure_defaults();
    for(unsigned i=0;i<count;i++)if(mounts[i].mount==node->mount) {
        if(node->mount_id!=mounts[i].device_id)return -ESTALE;
        if(node->filesystem!=node->mount->type)return -EINVAL;
        if(!mounts[i].open_refs)return -EINVAL;
        struct inode_size_lease *lease=inode_lease(node);
        if(!lease || !lease->refs)return -EINVAL;
        if(lease_conflicts(lease,node))return -ESTALE;
        --mounts[i].open_refs;
        if(!--lease->refs) {
            /* A final inode owner cannot retain an advisory lock. In
             * particular, an unlinked inode may be immediately recycled. */
            for(unsigned n=0;n<NAS_VFS_FLOCK_LIMIT;n++)
                if(flock_matches_node(&vfs_flocks[n],node))
                    vfs_flocks[n]=(struct vfs_flock_entry){0};
            /* Publish the release BEFORE invoking the backend: tmpfs
             * orphan cleanup may now query nas_vfs_inode_is_pinned().
             * The current node is valid for this final owner invocation. */
            *lease=(struct inode_size_lease){0};
            if(node->filesystem && node->filesystem->last_inode_unpin)
                node->filesystem->last_inode_unpin(node);
        }
        return 0;
    }
    return -ENODEV;
}

/* Stable during a mount's lifetime even if preceding mount table entries
 * are removed. Device ID 0 is always invalid; no kernel-address leak. */
uint64_t nas_vfs_node_device(const struct nas_fs_node *node) {
    if(!node || !node->mount)return 0;
    ensure_defaults();
    for(unsigned i=0;i<count;i++)
        if(mounts[i].mount==node->mount)
            return node->mount_id==mounts[i].device_id ?
                mounts[i].device_id : 0;
    return 0;
}
