#include "vfs.h"
#include "lookup.h"
#include "posix_locks.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include <stddef.h>
#define NAS_VFS_MOUNT_CHUNK_SIZE 16U
#define NAS_VFS_MOUNT_MAX 256U
/* A lease holds size + Linux record locks for the lifetime of one inode
 * incarnation. Growing by address-stable chunks avoids the unrelated 256
 * inode hard limit; a raw realloc would invalidate record-lock pointers
 * borrowed by syscall operations. The kernel heap owns these chunks.
 * Chunks are retained for reuse, bounded by the high-watermark of live
 * File/cwd/mount references, not by total file creation churn. */
#define NAS_VFS_LEASE_CHUNK_SIZE 32U
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
struct inode_lease_chunk {
    struct inode_lease_chunk *next;
    struct inode_size_lease slots[NAS_VFS_LEASE_CHUNK_SIZE];
};
static struct inode_lease_chunk *lease_chunks;
/* In this single-CPU VFS a free slot is a lease with no owner. Expanding
 * does not move existing lock/size objects, and the same slot is reused
 * when the last File reference disappears. */
static struct inode_size_lease *free_inode_lease_slot(void) {
    for(struct inode_lease_chunk *chunk=lease_chunks;chunk;chunk=chunk->next)
        for(unsigned i=0;i<NAS_VFS_LEASE_CHUNK_SIZE;i++)
            if(!chunk->slots[i].refs)return &chunk->slots[i];
    struct inode_lease_chunk *chunk=nas_heap_calloc(1,sizeof(*chunk));
    if(!chunk)return NULL;
    chunk->next=lease_chunks;
    lease_chunks=chunk;
    return &chunk->slots[0];
}
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
    for(struct inode_lease_chunk *chunk=lease_chunks;chunk;chunk=chunk->next)
        for(unsigned i=0;i<NAS_VFS_LEASE_CHUNK_SIZE;i++) {
            const struct inode_size_lease *entry=&chunk->slots[i];
            if(entry->refs && entry->backend_inode==backend_inode)
                return true;
        }
    return false;
}
static struct inode_size_lease *inode_lease(const struct nas_fs_node *node) {
    if(!node || !node->mount)return NULL;
    for(struct inode_lease_chunk *chunk=lease_chunks;chunk;chunk=chunk->next)
        for(unsigned i=0;i<NAS_VFS_LEASE_CHUNK_SIZE;i++) {
            struct inode_size_lease *entry=&chunk->slots[i];
            if(entry->refs && entry->mount==node->mount &&
               entry->ino==node->ino)return entry;
        }
    return NULL;
}
/* An inode lease is scoped to its mount AND backend incarnation. A stale
 * reused inode number cannot steal a newer File's size or owner reference. */
static bool lease_conflicts(const struct inode_size_lease *lease,
                            const struct nas_fs_node *node) {
    return lease && lease->backend_inode!=node->inode;
}
bool nas_vfs_node_has_lease(const struct nas_fs_node *node) {
    if(vfs_node_status(node))return false;
    const struct inode_size_lease *lease=inode_lease(node);
    return lease && !lease_conflicts(lease,node);
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
    /* VFS owns a stable superblock and mountpoint snapshot for the entire
     * attachment lifetime. Its registration pointer is only an unmount
     * token. Slots are never relocated or compacted. */
    bool used;
    const struct nas_vfs_mount *registration;
    struct nas_vfs_mount superblock;
    char path[256];
    char covered_name[256];
    uint32_t open_refs,child_refs;
    uint64_t device_id;
    /* Genuine owning attachment anchors: these Paths own parent Dentry,
     * covered Dentry and all corresponding ancestor/mount references.
     * No second legacy inode pin exists beside the Dentry owner. A
     * missing bootstrap covered name has only a parent Path, not a fake
     * inode. The mount slot itself is stable until unregistration. */
    struct nas_vfs_path parent_path,covered_path;
};
/* The ONLY parent/covered identity authority is the Dentry owned by
 * each mount's Path. These helpers are implemented after the concrete
 * Dentry definition; no tuple copy can drift from the Path object. */
static const struct nas_fs_node *mount_parent_inode(
    const struct mount_slot *slot);
static const struct nas_fs_node *mount_covered_inode(
    const struct mount_slot *slot);
static uint64_t next_device_id=8;
/* Immutable-address attachment storage: 16 static boot slots and 16-slot
 * heap chunks. A published superblock never moves, even as capacity grows.
 * Reuse changes the monotonically increasing device incarnation. */
struct mount_slot_chunk {
    struct mount_slot_chunk *next;
    struct mount_slot slots[NAS_VFS_MOUNT_CHUNK_SIZE];
};
static struct mount_slot bootstrap_mounts[NAS_VFS_MOUNT_CHUNK_SIZE];
static struct mount_slot_chunk *mount_chunks;
static unsigned mount_slots=NAS_VFS_MOUNT_CHUNK_SIZE;
static struct mount_slot *mount_slot_at(unsigned index) {
    if(index<NAS_VFS_MOUNT_CHUNK_SIZE)return &bootstrap_mounts[index];
    index-=NAS_VFS_MOUNT_CHUNK_SIZE;
    for(struct mount_slot_chunk *chunk=mount_chunks;chunk;chunk=chunk->next) {
        if(index<NAS_VFS_MOUNT_CHUNK_SIZE)return &chunk->slots[index];
        index-=NAS_VFS_MOUNT_CHUNK_SIZE;
    }
    return NULL;
}
/* Heap allocation is only needed after all static slots have owners.
 * Keep allocated chunks for reuse so stale borrowed pointers can be
 * recognized without ever dereferencing freed mount metadata. */
static int grow_mount_slots(void) {
    if(mount_slots>=NAS_VFS_MOUNT_MAX)return -ENOSPC;
    struct mount_slot_chunk *chunk=nas_heap_calloc(1,sizeof(*chunk));
    if(!chunk)return -ENOMEM;
    if(!mount_chunks)mount_chunks=chunk;
    else {
        struct mount_slot_chunk *tail=mount_chunks;
        while(tail->next)tail=tail->next;
        tail->next=chunk;
    }
    mount_slots+=NAS_VFS_MOUNT_CHUNK_SIZE;
    return 0;
}
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
    for(unsigned i=0;i<mount_slots;i++)
        if(mount_slot_at(i)->used &&
           &mount_slot_at(i)->superblock==node->mount) {
            if(node->mount_id!=mount_slot_at(i)->device_id)return -ESTALE;
            if(node->filesystem!=node->mount->type)return -EINVAL;
            if(lease_conflicts(inode_lease(node),node))return -ESTALE;
            return node->filesystem->validate_inode ?
                   node->filesystem->validate_inode(node) : 0;
        }
    return -ENODEV;
}
/* Mountpoint lexical validation applies only to registration. Path routing
 * belongs to component namei and the owned Dentry/Mount graph. */
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
    unsigned slot=mount_slots;
    for(unsigned i=0;i<mount_slots;i++) {
        if(mount_slot_at(i)->used) {
            /* One registration token owns at most one live mount, even if
             * its caller later mutates the borrowed source descriptor. */
            if(mount_slot_at(i)->registration==mount ||
               !strcmp(mount_slot_at(i)->path,mount->mountpoint))return -EEXIST;
        } else if(slot==mount_slots)slot=i;
    }
    if(slot==mount_slots && mount_slots>=NAS_VFS_MOUNT_MAX)return -ENOSPC;
    /* Bind the attachment to the real parent inode. String prefix mount
     * selection alone is insufficient: a symlink alias or moved parent
     * could otherwise silently redirect a mounted subtree. */
    struct nas_fs_node parent={0},covered={0};
    bool has_parent=false,has_covered=false;
    unsigned parent_slot=mount_slots;
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
            /* A root-less bootstrap is opt-in, not an implicit behavior
             * of ordinary mounts. Never synthesize intermediate parents. */
            if(!mount->allow_missing_mountpoint ||
               strcmp(parent_path,"/"))return -ENOENT;
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
            for(unsigned i=0;i<mount_slots;i++)
                if(mount_slot_at(i)->used &&
                   &mount_slot_at(i)->superblock==parent.mount) {
                    parent_slot=i;break;
                }
            if(parent_slot==mount_slots)return -ENODEV;
            if(mount_slot_at(parent_slot)->child_refs==UINT32_MAX)
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
        /* Missing final targets are legal ONLY for explicitly designated
         * early-boot virtual attachments, never silently for mount(2). */
        if(covered_rc==-ENOENT && !mount->allow_missing_mountpoint)
            return -ENOENT;
    }
    if(next_device_id==UINT64_MAX)return -ENOSPC;
    /* Allocate the slot BEFORE acquiring owned parent/covered dentries.
     * A rejected allocation cannot leak an inode pin or partial mount. */
    if(slot==mount_slots) {
        int grow_rc=grow_mount_slots();
        if(grow_rc)return grow_rc;
    }
    struct mount_slot attached={0};
    if(has_parent) {
        /* Bind the exact existing parent directory to the same canonical
         * name that was checked above, BEFORE publishing this attachment.
         * A counted Path owns its inode, mount and parent Dentry chain. */
        char parent_path[256];
        size_t n=strlen(mount->mountpoint);
        while(n>1 && mount->mountpoint[n-1]!='/')--n;
        if(n>1)--n;
        memcpy(parent_path,mount->mountpoint,n);
        parent_path[n]=0;
        int rc=nas_vfs_path_bind(&parent,parent_path,
                                 &attached.parent_path);
        if(rc)return rc;
        if(has_covered) {
            rc=nas_vfs_path_bind(&covered,mount->mountpoint,
                                 &attached.covered_path);
            if(rc) {
                (void)nas_vfs_path_put(&attached.parent_path);
                return rc;
            }
        }
        const char *last=strrchr(mount->mountpoint,'/');
        if(!last || !last[1]) {
            if(attached.covered_path.dentry)
                (void)nas_vfs_path_put(&attached.covered_path);
            (void)nas_vfs_path_put(&attached.parent_path);
            return -EINVAL;
        }
        memcpy(attached.covered_name,last+1,strlen(last+1)+1);
    }
    /* All backing references acquired: publish at final immutable address.
     * No File, cwd or nested mount ever borrows a caller-owned descriptor. */
    attached.device_id=next_device_id++;
    attached.registration=mount;
    attached.superblock=*mount;
    memcpy(attached.path,mount->mountpoint,strlen(mount->mountpoint)+1);
    (*mount_slot_at(slot))=attached;
    mount_slot_at(slot)->superblock.mountpoint=mount_slot_at(slot)->path;
    mount_slot_at(slot)->used=true;
    if(has_parent)++mount_slot_at(parent_slot)->child_refs;
    return 0;
}
/* Root selection is the ONLY direct mount-table lookup. Everything else
 * is resolved by the ONE component-aware namei engine in lookup.c. This
 * replaces the second longest-prefix walk, which duplicated filesystem
 * lookup, skipped symlink and '..' semantics, and bypassed mount crossings.
 *
 * A CPIO-less direct-ELF bootstrap genuinely has no '/' inode. Namei
 * treats that ENOENT as permission to cross a separately attached /mnt
 * on its first component; we must NOT manufacture a synthetic root here.
 * A borrowed root inode needs no pin: open/path owners acquire one. */
int nas_vfs_lookup(const char *absolute,struct nas_fs_node *out) {
    if(!absolute || !out || absolute[0]!='/')return -EINVAL;
    if(strlen(absolute)>=256)return -ENAMETOOLONG;
    ensure_defaults();
    if(strcmp(absolute,"/")) {
        char canonical[256];
        /* Direct callers historically inspect the final symlink itself.
         * Intermediate symlinks, dot/dotdot and mounts now use namei. */
        return nas_fs_lookup_node(absolute,false,out,canonical);
    }
    for(unsigned i=0;i<mount_slots;i++) {
        const struct mount_slot *slot=mount_slot_at(i);
        if(!slot->used || strcmp(slot->path,"/"))continue;
        const struct nas_vfs_mount *mnt=&slot->superblock;
        int rc=mnt->type->root(mnt,out);
        if(!rc) {
            out->filesystem=mnt->type;
            out->mount=mnt;
            out->mount_id=slot->device_id;
        }
        return rc;
    }
    return -ENOENT;
}
/* Overlay fallback is NOT mount selection. The parent inode already tells
 * us the exact mounted upper layer. If it misses a name, the VFS traverses
 * its OWN pinned covered Dentry in the lower layer, relative to this one
 * attachment. No global longest-prefix retry, no other mount registry
 * search, no providers interpreting a VFS absolute pathname.
 *
 * This keeps the historical CPIO /tmp overlay while letting ordinary
 * disk, RAM, proc and independent third-party providers use one namei.
 * The covered parent remains pinned for the upper mount's lifetime. */
static int lookup_covered_underlay(const struct nas_fs_node *upper_parent,
                                   const char *absolute,
                                   struct nas_fs_node *out) {
    const struct mount_slot *slot=NULL;
    for(unsigned i=0;i<mount_slots;i++) {
        if(mount_slot_at(i)->used &&
           &mount_slot_at(i)->superblock==upper_parent->mount) {
            slot=mount_slot_at(i);break;
        }
    }
    if(!slot || !slot->superblock.fallback_on_miss)return -ENOENT;
    const struct nas_fs_node *covered=mount_covered_inode(slot);
    if(!covered)return -ENOENT;
    /* The canonical name, not the user's raw spelling, is the sole
     * transitional projection needed by the legacy CPIO overlay. A
     * missing/moved attachment cannot accidentally fall through to an
     * unrelated pathname in the underlying root. */
    if(!mount_matches(slot->path,absolute))return -ENOENT;
    const char *tail=absolute+strlen(slot->path);
    while(*tail=='/')++tail;
    if(!*tail)return -ENOENT;
    int rc=vfs_node_status(covered);
    if(rc)return rc;
    struct nas_fs_node current=*covered;
    while(*tail) {
        if(!current.is_dir)return -ENOTDIR;
        char name[256];unsigned n=0;
        while(tail[n] && tail[n]!='/') {
            if(n>=255)return -ENAMETOOLONG;
            name[n]=tail[n];++n;
        }
        name[n]=0;
        if(!n || (n==1 && name[0]=='.') ||
           (n==2 && name[0]=='.' && name[1]=='.'))
            return -EINVAL;
        struct nas_fs_node next={0};
        rc=vfs_node_status(&current);
        if(rc)return rc;
        rc=current.filesystem->lookup_child(&current,name,&next);
        if(rc)return rc;
        next.mount=current.mount;
        next.mount_id=current.mount_id;
        next.filesystem=current.filesystem;
        current=next;
        tail+=n;
        while(*tail=='/')++tail;
    }
    *out=current;
    return 0;
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
    for(unsigned i=0;i<mount_slots;i++) {
        const struct mount_slot *slot=mount_slot_at(i);
        if(!slot->used)continue;
        const struct nas_vfs_mount *mnt=&slot->superblock;
        const struct nas_fs_node *anchor=mount_parent_inode(slot);
        if(anchor) {
            /* Match the EXACT owner of the mounted parent Dentry; no
             * parallel ino/pointer/mount tuple may become stale. */
            if(strcmp(name,slot->covered_name) ||
               !parent || parent->mount!=anchor->mount ||
               parent->mount_id!=anchor->mount_id ||
               parent->ino!=anchor->ino ||
               parent->inode!=anchor->inode)
                continue;
        } else if(strcmp(mnt->mountpoint,absolute))continue;
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
        return lookup_covered_underlay(parent,absolute,out);
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
/* A real positive Dentry graph, not a string wrapper. Every Dentry owns
 * exactly one backend inode+mount lease and one parent Dentry reference;
 * a File/FS context owns the leaf. Cache entries only survive while they
 * have live owners. This is deliberately NOT a global unbounded negative
 * dentry cache, nor a second filesystem implementation. */
struct nas_vfs_dentry {
    struct nas_vfs_dentry *next,*parent,*hash_next;
    struct nas_fs_node node;
    unsigned refs;
    bool detached;
    char name[256]; /* '/' root or '/mnt' for direct-ELF rootless bootstrap */
};
static struct nas_vfs_dentry *dentry_live;
/* A positive Dentry has two independent lifetimes: the name lookup index
 * contains only ACTIVE names, but File/FS/Mount refs can retain a detached
 * Dentry until the final put. A separate intrusive list owns inspection
 * and eventual destruction; neither list allocates on lookup.
 *
 * Names are keyed by parent Dentry AND mount incarnation. In particular,
 * the covered directory and a mounted root may have the same parent/name
 * while still denoting distinct inode objects in different mounts.
 * Single-CPU policy applies to all three ISAs until the future SMP phase. */
#define NAS_VFS_DENTRY_BUCKETS 257U
static struct nas_vfs_dentry *dentry_buckets[NAS_VFS_DENTRY_BUCKETS];
static unsigned dentry_bucket(const struct nas_vfs_dentry *parent,
                              const char *name,uint64_t mount_id) {
    uint32_t hash=2166136261U;
    uintptr_t address=(uintptr_t)parent;
    for(unsigned i=0;i<sizeof(address);i++) {
        hash=(hash^(uint8_t)address)*16777619U;
        address>>=8;
    }
    for(unsigned i=0;i<8;i++) {
        hash=(hash^(uint8_t)mount_id)*16777619U;
        mount_id>>=8;
    }
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)
        hash=(hash^*p)*16777619U;
    return hash%NAS_VFS_DENTRY_BUCKETS;
}
static void dentry_index_insert(struct nas_vfs_dentry *d) {
    unsigned bucket=dentry_bucket(d->parent,d->name,d->node.mount_id);
    d->hash_next=dentry_buckets[bucket];
    dentry_buckets[bucket]=d;
}
/* Detaching does NOT put a Path or inode: existing open owners must keep
 * working after unlink, rename-overwrite and same-name inode reuse. */
static bool dentry_index_remove(struct nas_vfs_dentry *d) {
    unsigned bucket=dentry_bucket(d->parent,d->name,d->node.mount_id);
    struct nas_vfs_dentry **scan=&dentry_buckets[bucket];
    while(*scan && *scan!=d)scan=&(*scan)->hash_next;
    if(!*scan)return false;
    *scan=d->hash_next;
    d->hash_next=NULL;
    return true;
}
static void dentry_detach(struct nas_vfs_dentry *d) {
    if(!d || d->detached)return;
    (void)dentry_index_remove(d);
    d->detached=true;
}
static const struct nas_fs_node *mount_parent_inode(
    const struct mount_slot *slot) {
    if(!slot || !slot->parent_path.dentry)return NULL;
    return &slot->parent_path.dentry->node;
}
static const struct nas_fs_node *mount_covered_inode(
    const struct mount_slot *slot) {
    if(!slot || !slot->covered_path.dentry)return NULL;
    return &slot->covered_path.dentry->node;
}
static bool same_vnode(const struct nas_fs_node *a,
                       const struct nas_fs_node *b) {
    return a->mount==b->mount && a->mount_id==b->mount_id &&
           a->ino==b->ino && a->inode==b->inode &&
           a->filesystem==b->filesystem;
}
static int dentry_ref(struct nas_vfs_dentry *entry) {
    if(!entry || !entry->refs || entry->refs==UINT32_MAX)
        return -EMFILE;
    ++entry->refs;
    return 0;
}
static int dentry_put(struct nas_vfs_dentry *entry) {
    if(!entry || !entry->refs)return -EINVAL;
    int first_error=0;
    /* Releasing one deep Path may drop its entire ancestor chain. A
     * recursive destructor consumes a C frame per path component and
     * could overflow NaSeed's bounded kernel stack. Walk iteratively. */
    while(entry) {
        if(!entry->refs)return first_error ? first_error : -EINVAL;
        if(--entry->refs)break;
        struct nas_vfs_dentry **slot=&dentry_live;
        while(*slot && *slot!=entry)slot=&(*slot)->next;
        if(!*slot)return -EIO;
        *slot=entry->next;
        if(!entry->detached && !dentry_index_remove(entry) && !first_error)
            first_error=-EIO;
        struct nas_vfs_dentry *parent=entry->parent;
        int rc=nas_vfs_node_unpin(&entry->node);
        if(rc && !first_error)first_error=rc;
        if(!nas_heap_free(entry) && !first_error)first_error=-EIO;
        /* Keep releasing parent owners even if a provider reported an
         * error: otherwise a failed final inode callback leaks mounts. */
        entry=parent;
    }
    return first_error;
}
static struct nas_vfs_dentry *dentry_find(struct nas_vfs_dentry *parent,
                                         const char *name,
                                         const struct nas_fs_node *node) {
    unsigned bucket=dentry_bucket(parent,name,node->mount_id);
    struct nas_vfs_dentry *d=dentry_buckets[bucket];
    while(d) {
        /* dentry_detach unlinks this entry from its hash chain. Retain
         * the successor first so same-name inode reuse never skips a
         * colliding entry (or returns an already detached inode). */
        struct nas_vfs_dentry *next=d->hash_next;
        if(d->parent==parent && !strcmp(d->name,name) &&
           d->node.mount_id==node->mount_id &&
           d->node.mount==node->mount) {
            if(same_vnode(&d->node,node))return d;
            dentry_detach(d);
        }
        d=next;
    }
    return NULL;
}
static int dentry_acquire(struct nas_vfs_dentry *parent,const char *name,
                          const struct nas_fs_node *inode,
                          struct nas_vfs_dentry **result) {
    struct nas_vfs_dentry *found=dentry_find(parent,name,inode);
    if(found) {
        int rc=dentry_ref(found);
        if(!rc)*result=found;
        return rc;
    }
    struct nas_vfs_dentry *d=nas_heap_calloc(1,sizeof(*d));
    if(!d)return -ENOMEM;
    int rc=nas_vfs_node_pin(inode);
    if(rc) { (void)nas_heap_free(d);return rc; }
    if(parent && (rc=dentry_ref(parent))) {
        (void)nas_vfs_node_unpin(inode);
        (void)nas_heap_free(d);
        return rc;
    }
    size_t length=strlen(name);
    if(length>=sizeof(d->name)) {
        if(parent)(void)dentry_put(parent);
        (void)nas_vfs_node_unpin(inode);
        (void)nas_heap_free(d);
        return -ENAMETOOLONG;
    }
    memcpy(d->name,name,length+1);
    d->node=*inode;
    d->refs=1;
    d->parent=parent;
    d->next=dentry_live;
    dentry_live=d;
    dentry_index_insert(d);
    *result=d;
    return 0;
}
int nas_vfs_path_bind(const struct nas_fs_node *inode,
                      const char *canonical,struct nas_vfs_path *out) {
    if(!inode || !canonical || !out || canonical[0]!='/')
        return -EINVAL;
    *out=(struct nas_vfs_path){0};
    size_t length=strlen(canonical);
    if(length>=256)return -ENAMETOOLONG;
    struct nas_vfs_dentry *current=NULL;
    char component_path[256];
    /* Resolve the root first. If an early direct ELF has no CPIO root,
     * an explicit bootstrap mount such as '/mnt' is a root Dentry with
     * an absolute mountpoint name, never a fabricated '/' inode. */
    struct nas_fs_node root;
    char resolved[256];
    int rc=nas_fs_lookup_node("/",true,&root,resolved);
    if(!rc) {
        rc=dentry_acquire(NULL,"/",&root,&current);
        if(rc)return rc;
    } else if(rc!=-ENOENT)return rc;
    if(length==1) {
        if(!current)return -ENOENT;
        if(!same_vnode(&current->node,inode)) {
            (void)dentry_put(current);return -ESTALE;
        }
    } else {
        size_t pos=1,used=1;
        component_path[0]='/';component_path[1]=0;
        while(pos<length) {
            size_t start=pos;
            while(pos<length && canonical[pos]!='/')++pos;
            size_t n=pos-start;
            if(!n || (n==1 && canonical[start]=='.') ||
               (n==2 && canonical[start]=='.' && canonical[start+1]=='.')) {
                rc=-EINVAL;break;
            }
            if(used>1)component_path[used++]='/';
            memcpy(component_path+used,canonical+start,n);
            used+=n;component_path[used]=0;
            struct nas_fs_node next_node;
            rc=nas_fs_lookup_node(component_path,true,&next_node,resolved);
            if(rc)break;
            if(strcmp(resolved,component_path)) {rc=-ESTALE;break;}
            struct nas_vfs_dentry *next=NULL;
            const char *name=current ? canonical+start : component_path;
            char basename[256];
            if(current) {
                memcpy(basename,name,n);
                basename[n]=0;
                name=basename;
            }
            rc=dentry_acquire(current,name,&next_node,&next);
            if(rc)break;
            if(current && dentry_put(current)) {
                (void)dentry_put(next);return -EIO;
            }
            current=next;
            if(pos<length)++pos;
        }
        if(rc) {
            if(current)(void)dentry_put(current);
            return rc;
        }
        if(!current || !same_vnode(&current->node,inode)) {
            if(current)(void)dentry_put(current);
            return -ESTALE;
        }
    }
    out->mount=current->node.mount;
    out->mount_id=current->node.mount_id;
    out->dentry=current;
    return 0;
}
int nas_vfs_path_get(const struct nas_vfs_path *source,
                     struct nas_vfs_path *out) {
    if(!source || !out || !source->dentry)return -EINVAL;
    int rc=dentry_ref(source->dentry);
    if(!rc)*out=*source;
    return rc;
}
int nas_vfs_path_put(struct nas_vfs_path *path) {
    if(!path || !path->dentry)return -EINVAL;
    struct nas_vfs_dentry *d=path->dentry;
    *path=(struct nas_vfs_path){0};
    return dentry_put(d);
}
int nas_vfs_path_string(const struct nas_vfs_path *path,
                        char absolute[256]) {
    if(!path || !path->dentry || !absolute)return -EINVAL;
    if(path->mount!=path->dentry->node.mount ||
       path->mount_id!=path->dentry->node.mount_id)return -ESTALE;
    struct nas_vfs_dentry *reverse[256];
    unsigned n=0;
    for(struct nas_vfs_dentry *d=path->dentry;d;d=d->parent) {
        if(n==256)return -ENAMETOOLONG;
        if(d->detached)return -ENOENT;
        reverse[n++]=d;
    }
    unsigned used=0;
    while(n) {
        struct nas_vfs_dentry *d=reverse[--n];
        const char *name=d->name;
        size_t len=strlen(name);
        if(!used) {
            if(name[0]!='/')return -ESTALE;
            if(len>=256)return -ENAMETOOLONG;
            memcpy(absolute,name,len);
            used=(unsigned)len;
        } else {
            if(used>1) {
                if(used+1>=256)return -ENAMETOOLONG;
                absolute[used++]='/';
            }
            if(used+len>=256)return -ENAMETOOLONG;
            memcpy(absolute+used,name,len);
            used+=(unsigned)len;
        }
    }
    absolute[used]=0;
    return 0;
}
/* Namespace transactions invoke these only AFTER the backend has succeeded,
 * using parent Path owners acquired BEFORE the mutation. No allocation
 * or pathname relookup occurs after the irreversible backend operation.
 * A replaced/unlinked Dentry stays alive but unreachable through namei. */
static void dentry_notice_unlink(const struct nas_vfs_path *parent,
                                 const char *name) {
    for(struct nas_vfs_dentry *d=dentry_live;d;d=d->next)
        if(!d->detached && d->parent==parent->dentry &&
           !strcmp(d->name,name))dentry_detach(d);
}
static void dentry_notice_rename(const struct nas_vfs_path *old_parent,
                                 const char *old_name,
                                 const struct nas_vfs_path *new_parent,
                                 const char *new_name) {
    if(old_parent->dentry==new_parent->dentry &&
       !strcmp(old_name,new_name))return;
    struct nas_vfs_dentry *moved=NULL;
    for(struct nas_vfs_dentry *d=dentry_live;d;d=d->next)
        if(!d->detached && d->parent==old_parent->dentry &&
           !strcmp(d->name,old_name)) {moved=d;break;}
    dentry_notice_unlink(new_parent,new_name);
    if(!moved)return;
    /* The hash key changes with both basename and parent. Remove first,
     * then transfer the independently counted parent owner, then publish
     * the new key. No lookup or allocation is needed after backend rename. */
    (void)dentry_index_remove(moved);
    if(old_parent->dentry!=new_parent->dentry) {
        if(dentry_ref(new_parent->dentry)) {
            moved->detached=true;
            return;
        }
        struct nas_vfs_dentry *previous=moved->parent;
        moved->parent=new_parent->dentry;
        if(previous)(void)dentry_put(previous);
    }
    memcpy(moved->name,new_name,strlen(new_name)+1);
    dentry_index_insert(moved);
}
static int dentry_prepare_parent(const char *full,
                                 const struct nas_fs_node *parent,
                                 struct nas_vfs_path *out) {
    const char *leaf=strrchr(full,'/');
    if(!leaf || !leaf[1])return -EINVAL;
    char base[256];
    size_t n=(size_t)(leaf-full);
    if(!n)n=1;
    memcpy(base,full,n);
    base[n]=0;
    return nas_vfs_path_bind(parent,base,out);
}
static bool valid_vfs_dirent(const struct nas_vfs_dirent *entry) {
    unsigned length=entry->name_len;
    if(!entry->ino || !length || length>255 || entry->name[length])
        return false;
    for(unsigned i=0;i<length;i++)
        if(!entry->name[i] || entry->name[i]=='/')return false;
    return true;
}
/* An attachment is owned by the VFS parent inode and child basename.
 * The parent filesystem cannot know about mountpoints created above it.
 * This identity match is the same as lookup_step's mount crossing: the
 * absolute string spelling of an old directory is NOT the mount owner. */
static bool mounted_child_of(const struct mount_slot *slot,
                             const struct nas_fs_node *parent) {
    const struct nas_fs_node *owner=mount_parent_inode(slot);
    return slot->used && owner && parent &&
           owner->mount==parent->mount &&
           owner->mount_id==parent->mount_id &&
           owner->ino==parent->ino &&
           owner->inode==parent->inode;
}
static int mounted_directory_entry(const struct mount_slot *slot,
                                   struct nas_vfs_dirent *entry) {
    /* The root inode number comes from the real mounted provider. Never
     * invent st_ino using a mount index or expose a kernel address. */
    struct nas_fs_node root={0};
    int rc=slot->superblock.type->root(&slot->superblock,&root);
    if(rc)return rc;
    if(!root.is_dir || !root.ino)return -EIO;
    unsigned length=(unsigned)strlen(slot->covered_name);
    if(!length || length>255)return -EIO;
    entry->ino=root.ino;entry->dtype=4;
    entry->name_len=(uint8_t)length;
    memcpy(entry->name,slot->covered_name,length+1);
    return 0;
}
/* VFS-owned mount dentries are visible through the SAME readdir path for
 * all providers. Synthetic boot mountpoints are inserted after "."/"..",
 * shifting underlying provider cookies by the number of VFS-only entries.
 * Real covered mountpoints retain their provider cookie/name, but report
 * the mounted ROOT inode, not the obscured underlay's inode number.
 * There is no filesystem-specific getdents syscall or vendor change. */
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
    unsigned virtual_count=0;
    for(unsigned i=0;i<mount_slots;i++)
        if(mounted_child_of(mount_slot_at(i),node) &&
           !mount_slot_at(i)->covered_path.dentry)++virtual_count;
    unsigned written=0;
    if(virtual_count && cursor<2) {
        unsigned first=(unsigned)(2-cursor);
        if(first>maximum)first=maximum;
        int got=node->filesystem->read_dir(node,cursor,entries,first);
        /* The common provider contract starts with "." and "..";
         * refusing a malformed backend is safer than inventing cookies. */
        if(got!=(int)first)return got<0?got:-EIO;
        for(unsigned i=0;i<first;i++) {
            const struct nas_vfs_dirent *e=&entries[i];
            const char *dot=(cursor+i)==0?".":"..";
            if(!valid_vfs_dirent(e) || strcmp(e->name,dot))
                return -EIO;
        }
        written=(unsigned)got;
        cursor+=(unsigned)got;
    }
    if(virtual_count && written<maximum && cursor<2+virtual_count) {
        unsigned ordinal=0;
        for(unsigned i=0;i<mount_slots && written<maximum;i++) {
            const struct mount_slot *slot=mount_slot_at(i);
            if(!mounted_child_of(slot,node) || slot->covered_path.dentry)
                continue;
            if(cursor>2+ordinal) {++ordinal;continue;}
            if(cursor!=2+ordinal)return -EIO;
            int rc=mounted_directory_entry(slot,&entries[written]);
            if(rc)return written?(int)written:rc;
            ++written;++cursor;++ordinal;
        }
    }
    if(written<maximum) {
        /* Provider logical cookies begin at zero. VFS synthetic entries
         * shift only child enumeration, never the provider's dot records. */
        uint64_t backend_cursor=cursor>=2+virtual_count ?
                                cursor-virtual_count : cursor;
        int got=node->filesystem->read_dir(node,backend_cursor,
                                            entries+written,maximum-written);
        if(got<0)return written?(int)written:got;
        if((unsigned)got>maximum-written)return -EIO;
        for(int j=0;j<got;j++) {
            struct nas_vfs_dirent *entry=&entries[written+(unsigned)j];
            /* Validate the UNTRUSTED provider entry before strcmp() or
             * replacing its fields with the mounted child metadata.
             * A malformed unterminated name must not be dereferenced
             * beyond the buffer or hidden by a covered mountpoint. */
            if(!valid_vfs_dirent(entry))return -EIO;
            /* A real covered mountpoint is one directory entry, but its
             * inode must be the visible mount root (Linux dcache rule). */
            for(unsigned i=0;i<mount_slots;i++) {
                const struct mount_slot *slot=mount_slot_at(i);
                if(!mounted_child_of(slot,node) || !slot->covered_path.dentry ||
                   strcmp(entry->name,slot->covered_name))continue;
                int rc=mounted_directory_entry(slot,entry);
                if(rc)return rc;
                break;
            }
        }
        written+=(unsigned)got;
    }
    for(unsigned i=0;i<written;i++)
        if(!valid_vfs_dirent(&entries[i]))return -EIO;
    return (int)written;
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
    for(unsigned i=0;i<mount_slots;i++) {
        if(!mount_slot_at(i)->used)continue;
        /* VFS-owned path snapshot is protected against ancestor rename
         * and unlink independently of caller's mount descriptor. */
        if(mount_matches(path,mount_slot_at(i)->path))return true;
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
                       struct nas_fs_node *owned,char canonical[256],
                       struct nas_vfs_path *owned_path) {
    if(!path || path[0]!='/' || !options || !owned || !canonical)
        return -EINVAL;
    if(owned_path)*owned_path=(struct nas_vfs_path){0};
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
        /* Grow the reusable lease pool BEFORE making a visible name.
         * A heap allocation failure must not leave O_CREAT side effects.
         * Open already reserved the process FD and global File slot. */
        if(!free_inode_lease_slot())return -ENOMEM;
        /* Never write a regular inode when the caller requires a dir. */
        if(options->directory_only)return -ENOTDIR;
        if(options->truncate && !options->path_only && !options->access)
            return -EINVAL;
        /* A dangling symlink's old name already exists. Create only its
         * missing final target via the shared component walker. */
        char target[256];
        rc=nas_fs_resolve_create_target(path,target);
        if(rc)return rc;
        rc=nas_vfs_create(target,options->create_mode);
        if(rc)return rc;
        rc=nas_fs_lookup_node(target,follow,&node,canonical);
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
    /* The VFS, not the syscall ABI, owns the Dentry/Open transaction.
     * Bind AFTER exact-inode pin, BEFORE side effects or File publication.
     * A failure releases the inode and never produces half an open. */
    struct nas_vfs_path file_path={0};
    if(owned_path && node.is_dir) {
        rc=nas_vfs_path_bind(&node,canonical,&file_path);
        if(rc) {
            (void)nas_vfs_node_unpin(&node);
            return rc;
        }
    }
    if(options->truncate && !options->path_only) {
        rc=nas_vfs_resize(&node,0);
        if(rc) {
            if(file_path.dentry)(void)nas_vfs_path_put(&file_path);
            (void)nas_vfs_node_unpin(&node);
            return rc;
        }
        node.size=0;
    }
    *owned=node; /* transfer inode to shared File */
    if(owned_path)*owned_path=file_path; /* transfer Path to same File */
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
int nas_vfs_sync_all(void) {
    ensure_defaults();
    int first_error=0;
    for(unsigned i=0;i<mount_slots;i++) {
        if(!mount_slot_at(i)->used)continue;
        const struct nas_vfs_mount *mounted=&mount_slot_at(i)->superblock;
        if(mounted->read_only || mounted->type->read_only)continue;
        struct nas_fs_node root={0};
        int rc=mounted->type->root(mounted,&root);
        if(rc) {
            if(!first_error)first_error=rc;
            continue;
        }
        root.mount=mounted;
        root.filesystem=mounted->type;
        root.mount_id=mount_slot_at(i)->device_id;
        rc=nas_vfs_sync(&root);
        if(rc && !first_error)first_error=rc;
    }
    return first_error;
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
    /* Backend capabilities are inode-kind specific: a filesystem may safely
     * orphan an opened file but still lack a nonrecursive directory protocol.
     * Fail closed only when that kind of unlink cannot preserve ownership. */
    if(!parent.filesystem->unlink_open_safe) {
        struct nas_fs_node target;
        int looked_up=nas_vfs_lookup(canonical,&target);
        /* rmdir(open directory) is just as unsafe as unlink(open file).
         * Directory st_nlink normally counts '.' and a parent's entry,
         * so the last-file-link threshold cannot be used for directories. */
        if(!looked_up && target.mount==parent.mount && target.ino &&
           inode_lease(&target) &&
           (((flags&0x200ULL) &&
             !parent.filesystem->unlink_open_dir_safe) ||
            (!(flags&0x200ULL) && target.nlink<=1 &&
             !parent.filesystem->unlink_open_file_safe)))
            return -EBUSY;
        if(looked_up && looked_up!=-ENOENT)return looked_up;
    }
    if(!parent.filesystem->unlink)return -EOPNOTSUPP;
    struct nas_vfs_path parent_path={0};
    rc=dentry_prepare_parent(canonical,&parent,&parent_path);
    if(rc)return rc;
    rc=parent.filesystem->unlink(&parent,vfs_leaf(canonical),flags);
    if(!rc)dentry_notice_unlink(&parent_path,vfs_leaf(canonical));
    if(nas_vfs_path_put(&parent_path))return -EIO;
    return rc;
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
    /* A directory cannot move beneath itself, regardless of provider.
     * Use canonical component boundaries instead of raw user spelling:
     * an alias or symlink to a parent is already resolved by namei. */
    struct nas_fs_node source_for_cycle;
    char current_source[256];
    int source_rc=nas_fs_lookup_node(old_canonical,false,
                                      &source_for_cycle,current_source);
    if(source_rc)return source_rc;
    if(source_for_cycle.is_dir &&
       strcmp(old_canonical,new_canonical) &&
       mount_matches(old_canonical,new_canonical))
        return -EINVAL;
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
    if(!old_parent.filesystem->rename)return -EOPNOTSUPP;
    struct nas_vfs_path from={0},to={0};
    rc=dentry_prepare_parent(old_canonical,&old_parent,&from);
    if(rc)return rc;
    rc=dentry_prepare_parent(new_canonical,&new_parent,&to);
    if(rc) {
        (void)nas_vfs_path_put(&from);
        return rc;
    }
    rc=old_parent.filesystem->rename(&old_parent,vfs_leaf(old_canonical),
                                      &new_parent,vfs_leaf(new_canonical),
                                      flags);
    if(!rc)dentry_notice_rename(&from,vfs_leaf(old_canonical),
                                &to,vfs_leaf(new_canonical));
    int dropped=nas_vfs_path_put(&from);
    if(nas_vfs_path_put(&to) || dropped)return -EIO;
    return rc;
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

/* Unmount by registration token, never by a mutable descriptor's path.
 * A stable internal slot is reclaimed only when all owned File references
 * and children are gone; other slots never move. */
int nas_vfs_unregister_mount(const struct nas_vfs_mount *registration) {
    if(!registration)return -EINVAL;
    ensure_defaults();
    unsigned at=mount_slots;
    for(unsigned i=0;i<mount_slots;i++)
        if(mount_slot_at(i)->used && mount_slot_at(i)->registration==registration) {
            at=i;break;
        }
    if(at==mount_slots)return -ENOENT;
    if(mount_slot_at(at)->superblock.permanent || mount_slot_at(at)->open_refs ||
       mount_slot_at(at)->child_refs)return -EBUSY;
    /* The counted attachment graph already owns child_refs. Every nested
     * child has a pinned parent Path; scanning string prefixes would
     * incorrectly define topology from spelling instead of ownership.
     * Rootless bootstrap children are attached to their live parent
     * mount, or are independently rooted with no structural parent. */
    unsigned parent_at=mount_slots;
    const struct nas_fs_node *owner=mount_parent_inode(mount_slot_at(at));
    const struct nas_fs_node *covered=mount_covered_inode(mount_slot_at(at));
    if(owner) {
        for(unsigned i=0;i<mount_slots;i++)
            if(mount_slot_at(i)->used &&
               &mount_slot_at(i)->superblock==owner->mount) {
                parent_at=i;break;
            }
        if(parent_at==mount_slots ||
           !mount_slot_at(parent_at)->child_refs ||
           !inode_lease(owner) ||
           (covered && !inode_lease(covered)))return -EIO;
    }
    /* Drop covered before parent: a covered Dentry can own its parent.
     * Both are real Path owners; do not release an extra inode pin. */
    if(mount_slot_at(at)->covered_path.dentry &&
       nas_vfs_path_put(&mount_slot_at(at)->covered_path))return -EIO;
    if(mount_slot_at(at)->parent_path.dentry &&
       nas_vfs_path_put(&mount_slot_at(at)->parent_path))return -EIO;
    if(parent_at!=mount_slots)--mount_slot_at(parent_at)->child_refs;
    (*mount_slot_at(at))=(struct mount_slot){0};
    return 0;
}
int nas_vfs_node_pin(const struct nas_fs_node *node) {
    if(!node || !node->filesystem || !node->mount ||
       (!node->ino && !node->inode))return -EINVAL;
    int status=vfs_node_status(node);
    if(status)return status;
    for(unsigned i=0;i<mount_slots;i++)
        if(mount_slot_at(i)->used && &mount_slot_at(i)->superblock==node->mount) {
        if(mount_slot_at(i)->open_refs==UINT32_MAX)return -EMFILE;
        struct inode_size_lease *lease=inode_lease(node);
        if(lease) {
            if(lease_conflicts(lease,node))return -ESTALE;
            if(lease->refs==UINT32_MAX)return -EMFILE;
            ++lease->refs;
        } else {
            lease=free_inode_lease_slot();
            if(!lease)return -ENOMEM;
            *lease=(struct inode_size_lease){
                .mount=node->mount,.ino=node->ino,
                .backend_inode=node->inode,.size=node->size,.refs=1
            };
        }
        ++mount_slot_at(i)->open_refs;return 0;
    }
    return -ENODEV;
}
int nas_vfs_node_unpin(const struct nas_fs_node *node) {
    if(!node || !node->mount || !node->filesystem)return -EINVAL;
    ensure_defaults();
    for(unsigned i=0;i<mount_slots;i++)
        if(mount_slot_at(i)->used && &mount_slot_at(i)->superblock==node->mount) {
        if(node->mount_id!=mount_slot_at(i)->device_id)return -ESTALE;
        if(node->filesystem!=node->mount->type)return -EINVAL;
        if(!mount_slot_at(i)->open_refs)return -EINVAL;
        struct inode_size_lease *lease=inode_lease(node);
        if(!lease || !lease->refs)return -EINVAL;
        if(lease_conflicts(lease,node))return -ESTALE;
        --mount_slot_at(i)->open_refs;
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
    for(unsigned i=0;i<mount_slots;i++)
        if(mount_slot_at(i)->used && &mount_slot_at(i)->superblock==node->mount)
            return node->mount_id==mount_slot_at(i)->device_id ?
                mount_slot_at(i)->device_id : 0;
    return 0;
}
