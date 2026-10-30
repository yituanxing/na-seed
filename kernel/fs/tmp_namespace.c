#include "../tmpfs.h"
#include "../bootfs.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../mm/frame.h"
#include "../arch/arch.h"

/* A dentry is a (parent inode, one basename, child inode) edge, as in
 * Linux dcache / NaOS vnode trees. The namespace owns three indexes:
 * hashed (parent, name) lookup, one ordered child list per directory, and
 * the unique alias of each directory inode (directories cannot be linked).
 * The ordered list shares stable slab entries with the lookup table; there
 * is no second pathname, task cwd cache or shadow namespace authority.
 * Directory rename changes ONE edge; children retain their parent inode. */
#define TMP_NAME_BUCKETS 2048U
#define TMP_DENTRIES_PER_SLAB (4096U / sizeof(struct nas_tmp_dentry))
struct tmp_dentry_slab {
    struct nas_tmp_dentry entries[TMP_DENTRIES_PER_SLAB];
};
_Static_assert(TMP_DENTRIES_PER_SLAB>=2, "dentries fit a frame");
_Static_assert(sizeof(struct tmp_dentry_slab)<=4096, "dentry slab too large");
static struct nas_tmp_dentry *tmp_free_dentries;
static struct nas_tmp_dentry *tmp_name_buckets[TMP_NAME_BUCKETS];
static struct nas_tmp_directory tmp_root_children;

static const void *tmp_root_inode(void) {
    const struct bootfs_entry *root=bootfs_find("/tmp");
    return bootfs_is_dir(root) ? (const void *)root : 0;
}
static bool leaf_valid(const char *name) {
    if(!name || !name[0] || strchr(name,'/') ||
       !strcmp(name,".") || !strcmp(name,".."))return false;
    return strlen(name)<256;
}
static unsigned name_bucket(const void *parent,const char *name) {
    /* Hash identity AND basename: sibling names on different directories do
     * not form one long chain. No backend inode numbers or paths are keys. */
    /* Only eleven bits index the table. FNV-1a/32 avoids expensive RV32I
     * 64-bit multiplication in every lookup and never requires an M opcode. */
    uint32_t h=2166136261U;
    uintptr_t identity=(uintptr_t)parent;
    for(unsigned i=0;i<sizeof(identity);i++) {
        h^=(uint8_t)(identity>>(i*8));h*=16777619U;
    }
    for(const unsigned char *p=(const unsigned char *)name;*p;p++) {
        h^=*p;h*=16777619U;
    }
    return (unsigned)(h&(TMP_NAME_BUCKETS-1));
}
static struct nas_tmp_dentry *find_child(const void *parent,const char *name) {
    if(!parent || !leaf_valid(name))return 0;
    for(struct nas_tmp_dentry *d=tmp_name_buckets[name_bucket(parent,name)];
        d;d=d->hash_next)
        if(d->parent_inode==parent && !strcmp(d->name,name))return d;
    return 0;
}
static struct nas_tmp_dentry *find_inode(const void *inode) {
    if(!inode || inode==tmp_root_inode())return 0;
    const struct nas_tmp_file *directory=inode;
    if(!directory->used || !directory->is_dir)return 0;
    struct nas_tmp_dentry *d=directory->directory_alias;
    return d && d->used && d->inode==inode ? d : 0;
}
static struct nas_tmp_directory *directory_state(const void *parent) {
    if(!parent)return 0;
    if(parent==tmp_root_inode())return &tmp_root_children;
    struct nas_tmp_file *f=(struct nas_tmp_file *)(uintptr_t)parent;
    return find_inode(parent) ? &f->children : 0;
}
static bool parent_valid(const void *parent) {
    return directory_state(parent)!=0;
}
/* Every successful directory-entry change modifies the actual parent
 * directory's mtime/ctime. The writable /tmp root borrows a bootfs inode,
 * so its mutable metadata lives in the tmpfs provider, not in CPIO. */
static void parent_modified(const void *parent) {
    if(parent==tmp_root_inode())tmp_root_dir_modified();
    else tmp_dir_modified((struct nas_tmp_file *)(uintptr_t)parent);
}
static void invalidate_seek(struct nas_tmp_directory *directory) {
    directory->seek=0;directory->seek_index=0;
}
static void hash_attach(struct nas_tmp_dentry *d) {
    unsigned bucket=name_bucket(d->parent_inode,d->name);
    d->hash_next=tmp_name_buckets[bucket];
    tmp_name_buckets[bucket]=d;
}
static void hash_detach(struct nas_tmp_dentry *d) {
    unsigned bucket=name_bucket(d->parent_inode,d->name);
    struct nas_tmp_dentry **link=&tmp_name_buckets[bucket];
    while(*link && *link!=d)link=&(*link)->hash_next;
    if(*link)*link=d->hash_next;
    d->hash_next=0;
}
static void child_attach(struct nas_tmp_directory *directory,
                         struct nas_tmp_dentry *d) {
    d->directory_prev=directory->last;
    d->directory_next=0;
    if(directory->last)directory->last->directory_next=d;
    else directory->first=d;
    directory->last=d;
    directory->count++;
    invalidate_seek(directory);
}
static void child_detach(struct nas_tmp_directory *directory,
                         struct nas_tmp_dentry *d) {
    if(d->directory_prev)d->directory_prev->directory_next=d->directory_next;
    else directory->first=d->directory_next;
    if(d->directory_next)d->directory_next->directory_prev=d->directory_prev;
    else directory->last=d->directory_prev;
    d->directory_prev=d->directory_next=0;
    directory->count--;
    invalidate_seek(directory);
}
struct nas_tmp_file *tmp_find_child(const void *parent,const char *name) {
    struct nas_tmp_dentry *d=find_child(parent,name);
    return d?d->inode:0;
}
const struct nas_tmp_dentry *tmp_child_entry_inode(const void *parent,
                                                   unsigned index) {
    struct nas_tmp_directory *dir=directory_state(parent);
    if(!dir || index>=dir->count)return 0;
    struct nas_tmp_dentry *d=dir->first;
    unsigned ordinal=0;
    if(dir->seek && index>=dir->seek_index) {
        d=dir->seek;ordinal=dir->seek_index;
    }
    while(ordinal<index && d) {d=d->directory_next;ordinal++;}
    dir->seek=d;dir->seek_index=index;
    return d;
}
const void *tmp_dir_parent_inode(const void *dir) {
    if(!dir)return 0;
    if(dir==tmp_root_inode())return dir;
    struct nas_tmp_dentry *entry=find_inode(dir);
    return entry?entry->parent_inode:0;
}
/* Linux tmpfs accounts each live directory entry as 20 bytes of virtual
 * directory storage, in addition to the initial 40-byte directory inode.
 * This is derived from the ONE ordered child index: no second mutable size
 * cache can drift after rename, unlink, or failed insertion. */
uint64_t tmp_directory_stat_size(const struct nas_tmp_file *directory) {
    return directory && directory->is_dir ?
        40ULL + 20ULL * directory->children.count : 0;
}
uint64_t tmp_root_directory_stat_size(void) {
    return 40ULL + 20ULL * tmp_root_children.count;
}
static struct nas_tmp_dentry *tmp_insert(const void *parent,const char *name,
                                         struct nas_tmp_file *inode) {
    struct nas_tmp_directory *directory=directory_state(parent);
    if(!directory || !leaf_valid(name) || !inode || !inode->used ||
       find_child(parent,name) || (inode->is_dir && inode->directory_alias))
        return 0;
    /* The caller's Path-first namei already validates the Linux PATH_MAX
     * syscall input and each NAME_MAX component.  Backend insertion is by
     * stable parent inode; a directory FD may legally create children even
     * when the full ancestry cannot fit one pathname buffer. */
    if(!tmp_free_dentries) {
        uint64_t frame=nas_frame_alloc();
        if(!frame)return 0;
        struct tmp_dentry_slab *s=
            (struct tmp_dentry_slab *)(uintptr_t)(nas_hhdm_offset()+frame);
        memset(s,0,4096);
        for(unsigned i=TMP_DENTRIES_PER_SLAB;i>0;i--) {
            struct nas_tmp_dentry *slot=&s->entries[i-1];
            slot->free_next=tmp_free_dentries;
            tmp_free_dentries=slot;
        }
    }
    struct nas_tmp_dentry *entry=tmp_free_dentries;
    tmp_free_dentries=entry->free_next;
    *entry=(struct nas_tmp_dentry){0};
    entry->parent_inode=parent;
    memcpy(entry->name,name,strlen(name)+1);
    entry->inode=inode;
    entry->used=true;
    hash_attach(entry);
    child_attach(directory,entry);
    if(inode->is_dir)inode->directory_alias=entry;
    tmp_inode_link(inode);
    parent_modified(parent);
    return entry;
}
static void tmp_remove_entry(struct nas_tmp_dentry *d) {
    if(!d || !d->used)return;
    struct nas_tmp_file *inode=d->inode;
    const void *parent=d->parent_inode;
    struct nas_tmp_directory *directory=directory_state(parent);
    hash_detach(d);
    if(directory)child_detach(directory,d);
    if(inode->is_dir && inode->directory_alias==d)inode->directory_alias=0;
    *d=(struct nas_tmp_dentry){0};
    d->free_next=tmp_free_dentries;
    tmp_free_dentries=d;
    tmp_inode_unlink(inode);
    if(directory)parent_modified(parent);
}
struct nas_tmp_file *tmp_create_at(const void *parent,const char *name,
                                  bool dir,uint32_t mode) {
    if(!parent_valid(parent) || !leaf_valid(name) ||
       find_child(parent,name))return 0;
    struct nas_tmp_file *inode=tmp_inode_alloc(dir,mode);
    if(!inode)return 0;
    if(!tmp_insert(parent,name,inode)) {
        tmp_inode_discard(inode);
        return 0;
    }
    return inode;
}
/* Special filesystem nodes use the same inode/dentry lifetime as regular
 * files. Publish S_IFSOCK identity atomically: neither namei nor stat may
 * observe an intermediate S_IFREG inode at the destination. */
struct nas_tmp_file *tmp_mknod_at(const void *parent,const char *name,
                                 uint32_t mode) {
    if((mode&0170000U)!=0140000U || !parent_valid(parent) ||
       !leaf_valid(name) || find_child(parent,name))return 0;
    struct nas_tmp_file *inode=tmp_inode_alloc(false,mode);
    if(!inode)return 0;
    inode->mode=0140000U|(mode&07777U);
    if(!tmp_insert(parent,name,inode)) {
        tmp_inode_discard(inode);
        return 0;
    }
    return inode;
}
int tmp_mkdir_at(const void *parent,const char *name,uint64_t mode) {
    if(!parent_valid(parent))return -ENOENT;
    if(!leaf_valid(name))return -EINVAL;
    if(find_child(parent,name))return -EEXIST;
    return tmp_create_at(parent,name,true,(uint32_t)mode) ? 0 : -ENOSPC;
}
int tmp_symlink_at(const void *parent,const char *name,const char *target) {
    if(!target || !target[0])return -ENOENT;
    unsigned length=0;
    while(target[length])if(++length>=256)return -ENAMETOOLONG;
    if(!parent_valid(parent))return -ENOENT;
    if(!leaf_valid(name))return -EINVAL;
    if(find_child(parent,name))return -EEXIST;
    /* Build the complete typed inode, including its target bytes, before
     * publishing the basename. No transient regular-file dentry and no
     * unlink-on-write-error rollback are observable to concurrent namei. */
    struct nas_tmp_file *inode=tmp_inode_alloc(false,0777U);
    if(!inode)return -ENOSPC;
    inode->mode=0120000U|0777U;
    if(tmp_write_at(inode,0,(const uint8_t *)target,length)!=(int64_t)length) {
        tmp_inode_discard(inode);
        return -ENOMEM;
    }
    if(!tmp_insert(parent,name,inode)) {
        tmp_inode_discard(inode);
        return -ENOSPC;
    }
    return 0;
}
int tmp_link_at(const void *old_parent,const char *old_name,
                const void *new_parent,const char *new_name) {
    if(!parent_valid(old_parent) || !parent_valid(new_parent))return -ENOENT;
    struct nas_tmp_dentry *source=find_child(old_parent,old_name);
    if(!source)return -ENOENT;
    struct nas_tmp_file *inode=source->inode;
    if(inode->is_dir)return -EPERM;
    if(!leaf_valid(new_name))return -EINVAL;
    if(find_child(new_parent,new_name))return -EEXIST;
    if(inode->nlink==UINT32_MAX)return -EMLINK;
    return tmp_insert(new_parent,new_name,inode) ? 0 : -ENOSPC;
}
int tmp_unlink_at(const void *parent,const char *name,uint64_t flags) {
    const uint64_t removedir=0x200ULL;
    if(flags & ~removedir)return -EINVAL;
    if(!parent_valid(parent))return -ENOENT;
    struct nas_tmp_dentry *d=find_child(parent,name);
    if(!d)return -ENOENT;
    struct nas_tmp_file *inode=d->inode;
    if(inode->is_dir && !(flags&removedir))return -EISDIR;
    if(!inode->is_dir && (flags&removedir))return -ENOTDIR;
    if(inode->is_dir) {
        if(tmp_child_entry_inode(inode,0))return -ENOTEMPTY;
        /* A cwd may outlive its directory name. Linux permits rmdir(cwd):
         * the VFS-owned Path keeps the inode pinned until its final release,
         * while subsequent namei/getcwd reject the detached dentry. The
         * historical process cwd text cache is not a namespace owner and
         * must not forbid removing an otherwise empty directory. */
    }
    tmp_remove_entry(d);
    return 0;
}
/* Does parent belong to a directory's descendant chain? This prevents
 * rename(/tmp/a, /tmp/a/b/a) without string-prefix guessing. */
static bool under_inode(const void *parent,const void *directory) {
    const void *root=tmp_root_inode();
    for(unsigned depth=0;parent && depth<128;depth++) {
        if(parent==directory)return true;
        if(parent==root)return false;
        const struct nas_tmp_dentry *entry=find_inode(parent);
        if(!entry)return false;
        parent=entry->parent_inode;
    }
    return false;
}
int tmp_rename_at(const void *old_parent,const char *old_name,
                  const void *new_parent,const char *new_name,uint64_t flags) {
    if(flags&~1ULL)return -EINVAL;
    if(!parent_valid(old_parent) || !parent_valid(new_parent))return -ENOENT;
    if(!leaf_valid(old_name) || !leaf_valid(new_name))return -EINVAL;
    struct nas_tmp_dentry *source=find_child(old_parent,old_name);
    if(!source)return -ENOENT;
    if(old_parent==new_parent && !strcmp(old_name,new_name))return 0;
    struct nas_tmp_file *inode=source->inode;
    if(inode->is_dir && under_inode(new_parent,inode))return -EINVAL;
    struct nas_tmp_dentry *target=find_child(new_parent,new_name);
    if(target) {
        if(flags&1ULL)return -EEXIST;
        if(target->inode==inode)return 0; /* POSIX same inode no-op */
        if(inode->is_dir && !target->inode->is_dir)return -ENOTDIR;
        if(!inode->is_dir && target->inode->is_dir)return -EISDIR;
        if(target->inode->is_dir &&
           tmp_child_entry_inode(target->inode,0))return -ENOTEMPTY;
    }
    if(target)tmp_remove_entry(target); /* open replaced File retains inode */
    struct nas_tmp_directory *old_directory=directory_state(old_parent);
    struct nas_tmp_directory *new_directory=directory_state(new_parent);
    hash_detach(source);
    if(old_parent!=new_parent)child_detach(old_directory,source);
    else invalidate_seek(old_directory);
    source->parent_inode=new_parent;
    memcpy(source->name,new_name,strlen(new_name)+1); /* ONE dentry edge */
    hash_attach(source);
    if(old_parent!=new_parent)child_attach(new_directory,source);
    tmp_inode_touch(inode);
    parent_modified(old_parent);
    if(old_parent!=new_parent)parent_modified(new_parent);
    /* The syscall FS context owns cwd_path. Its next getcwd resolves the
     * live Dentry ancestry; tmpfs must not traverse Task or mutate a
     * process-local spelling cache as a second namespace authority. */
    return 0;
}
