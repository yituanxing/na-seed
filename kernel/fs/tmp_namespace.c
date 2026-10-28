#include "../tmpfs.h"
#include "../task.h"
#include "../bootfs.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../mm/frame.h"
#include "../arch/arch.h"

/* A dentry is a (parent inode, one basename, child inode) edge, as in
 * Linux dcache / NaOS vnode trees. Absolute paths exist only at the
 * syscall compatibility boundary. Directory rename is one edge update:
 * children retain their parents and no subtree pathname cache is rewritten.
 * This is the sole tmpfs namespace owner; sparse inode data is in tmpfs.c. */
#define TMP_DENTRIES_PER_SLAB ((4096U - sizeof(void *)) / sizeof(struct nas_tmp_dentry))
struct tmp_dentry_slab {
    struct tmp_dentry_slab *next;
    struct nas_tmp_dentry entries[TMP_DENTRIES_PER_SLAB];
};
_Static_assert(TMP_DENTRIES_PER_SLAB>=2, "dentries fit a frame");
_Static_assert(sizeof(struct tmp_dentry_slab)<=4096, "dentry slab too large");
static struct tmp_dentry_slab *tmp_dentry_slabs;

static const void *tmp_root_inode(void) {
    const struct bootfs_entry *root=bootfs_find("/tmp");
    return bootfs_is_dir(root) ? (const void *)root : 0;
}
static bool leaf_valid(const char *name) {
    if(!name || !name[0] || strchr(name,'/') ||
       !strcmp(name,".") || !strcmp(name,".."))return false;
    return strlen(name)<256;
}
static struct nas_tmp_dentry *find_child(const void *parent,const char *name) {
    if(!parent || !leaf_valid(name))return 0;
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++) {
            struct nas_tmp_dentry *d=&s->entries[i];
            if(d->used && d->parent_inode==parent &&
               !strcmp(d->name,name))return d;
        }
    return 0;
}
static struct nas_tmp_dentry *find_inode(const void *inode) {
    if(!inode)return 0;
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++)
            if(s->entries[i].used && s->entries[i].inode==inode)
                return &s->entries[i];
    return 0;
}
static bool parent_valid(const void *parent) {
    if(!parent)return false;
    if(parent==tmp_root_inode())return true;
    const struct nas_tmp_file *f=parent;
    return f->used && f->is_dir && find_inode(parent)!=0;
}
bool tmp_path(const char *path) {
    return path && !strncmp(path,"/tmp/",5) && path[5]!=0;
}
/* Resolve legacy absolute path strings by walking THE SAME dentry edges
 * used by VFS lookup_child(parent,name). No second flat path table. */
static struct nas_tmp_dentry *find_entry(const char *path) {
    if(!tmp_path(path))return 0;
    const void *parent=tmp_root_inode();
    if(!parent)return 0;
    const char *p=path+5;
    for(;;) {
        char leaf[256];unsigned n=0;
        while(p[n] && p[n]!='/') {
            if(n==255)return 0;
            leaf[n]=p[n];n++;
        }
        leaf[n]=0;
        struct nas_tmp_dentry *d=find_child(parent,leaf);
        if(!d)return 0;
        if(!p[n])return d;
        if(!d->inode || !d->inode->used || !d->inode->is_dir)return 0;
        parent=d->inode;p+=n+1;
        if(!*p)return 0;
    }
}
static int legacy_parent(const char *path,const void **parent,
                         char name[256]) {
    if(!tmp_path(path))return -EROFS;
    unsigned n=0,slash=0;
    for(;path[n] && n<256;n++)if(path[n]=='/')slash=n;
    if(n==256 || slash<4 || !path[slash+1])return -ENAMETOOLONG;
    size_t leaf=n-slash-1;
    if(!leaf || leaf>=256)return -EINVAL;
    memcpy(name,path+slash+1,leaf+1);
    if(!leaf_valid(name))return -EINVAL;
    if(slash==4) {
        *parent=tmp_root_inode();
        return *parent ? 0 : -ENOENT;
    }
    char dirname[256];
    memcpy(dirname,path,slash);dirname[slash]=0;
    struct nas_tmp_dentry *d=find_entry(dirname);
    if(!d)return -ENOENT;
    if(!d->inode || !d->inode->is_dir)return -ENOTDIR;
    *parent=d->inode;
    return 0;
}
int tmp_parent_check(const char *path) {
    const void *parent=0;char name[256];
    return legacy_parent(path,&parent,name);
}
struct nas_tmp_file *tmp_find(const char *path) {
    struct nas_tmp_dentry *d=find_entry(path);
    return d?d->inode:0;
}
struct nas_tmp_file *tmp_find_child(const void *parent,const char *name) {
    struct nas_tmp_dentry *d=find_child(parent,name);
    return d?d->inode:0;
}
const struct nas_tmp_dentry *tmp_child_entry_inode(const void *parent,
                                                   unsigned index) {
    if(!parent_valid(parent))return 0;
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++) {
            const struct nas_tmp_dentry *d=&s->entries[i];
            if(d->used && d->parent_inode==parent) {
                if(!index)return d;
                index--;
            }
        }
    return 0;
}
const struct nas_tmp_dentry *tmp_child_entry(const char *parent,unsigned index) {
    if(!parent)return 0;
    const void *inode=!strcmp(parent,"/tmp") ?
        tmp_root_inode():(const void *)tmp_find(parent);
    return tmp_child_entry_inode(inode,index);
}
const void *tmp_dir_parent_inode(const void *dir) {
    if(!dir)return 0;
    if(dir==tmp_root_inode())return dir;
    struct nas_tmp_dentry *entry=find_inode(dir);
    return entry?entry->parent_inode:0;
}
/* Reconstruct a path from edges for getcwd and legacy UNIX sockaddr. No
 * writable inode stores a pathname and no dentry stores its full pathname.
 * One static scratch buffer is safe under the current single-CPU kernel
 * invariant; the VFS copies/validates it before a syscall consumes it. */
static bool dentry_path(const struct nas_tmp_dentry *entry,char out[256]) {
    const struct nas_tmp_dentry *chain[128];
    unsigned depth=0;
    const void *root=tmp_root_inode();
    while(entry) {
        if(depth==128 || !entry->used)return false;
        chain[depth++]=entry;
        if(entry->parent_inode==root)break;
        entry=find_inode(entry->parent_inode);
        if(!entry)return false;
    }
    if(!depth || chain[depth-1]->parent_inode!=root)return false;
    unsigned pos=4;
    memcpy(out,"/tmp",4);
    while(depth) {
        const char *part=chain[--depth]->name;
        size_t n=strlen(part);
        if(!n || pos+n+1>=256)return false;
        out[pos++]='/';
        memcpy(out+pos,part,n);pos+=(unsigned)n;
    }
    out[pos]=0;return true;
}
const char *tmp_name(const struct nas_tmp_file *file) {
    static char canonical[256];
    if(!file || !file->used)return 0;
    const struct nas_tmp_dentry *entry=find_inode(file);
    return entry && dentry_path(entry,canonical) ? canonical : 0;
}
static struct nas_tmp_dentry *tmp_insert(const void *parent,const char *name,
                                         struct nas_tmp_file *inode) {
    if(!parent_valid(parent) || !leaf_valid(name) ||
       !inode || !inode->used || find_child(parent,name))return 0;
    /* Reject unreachable overlong paths before publishing a new dentry.
     * Namei's 256-byte bound applies to the entire ancestry, not only
     * the one-component basename. */
    size_t base_length=4;
    if(parent!=tmp_root_inode()) {
        const char *base=tmp_name(parent);
        if(!base)return 0;
        base_length=strlen(base);
    }
    if(base_length+1+strlen(name)>=256)return 0;
    struct nas_tmp_dentry *entry=0;
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s && !entry;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++)
            if(!s->entries[i].used){entry=&s->entries[i];break;}
    if(!entry) {
        uint64_t frame=nas_frame_alloc();
        if(!frame)return 0;
        struct tmp_dentry_slab *s=
            (struct tmp_dentry_slab *)(uintptr_t)(nas_hhdm_offset()+frame);
        memset(s,0,4096);
        s->next=tmp_dentry_slabs;tmp_dentry_slabs=s;
        entry=&s->entries[0];
    }
    entry->parent_inode=parent;
    memcpy(entry->name,name,strlen(name)+1);
    entry->inode=inode;
    entry->used=true;
    tmp_inode_link(inode);
    return entry;
}
static void tmp_remove_entry(struct nas_tmp_dentry *d) {
    if(!d || !d->used)return;
    struct nas_tmp_file *inode=d->inode;
    d->used=false;d->inode=0;d->parent_inode=0;d->name[0]=0;
    tmp_inode_unlink(inode);
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
struct nas_tmp_file *tmp_create(const char *path,bool dir,uint32_t mode) {
    const void *parent=0;char name[256];
    return legacy_parent(path,&parent,name) ? 0 :
           tmp_create_at(parent,name,dir,mode);
}
int tmp_mkdir_at(const void *parent,const char *name,uint64_t mode) {
    if(!parent_valid(parent))return -ENOENT;
    if(!leaf_valid(name))return -EINVAL;
    if(find_child(parent,name))return -EEXIST;
    return tmp_create_at(parent,name,true,(uint32_t)mode) ? 0 : -ENOSPC;
}
int tmp_mkdir(const char *path,uint64_t mode) {
    if(bootfs_find(path) || tmp_find(path))return -EEXIST;
    const void *parent=0;char name[256];
    int rc=legacy_parent(path,&parent,name);
    return rc?rc:tmp_mkdir_at(parent,name,mode);
}
int tmp_symlink_at(const void *parent,const char *name,const char *target) {
    if(!target || !target[0])return -ENOENT;
    unsigned length=0;
    while(target[length])if(++length>=256)return -ENAMETOOLONG;
    if(!parent_valid(parent))return -ENOENT;
    if(find_child(parent,name))return -EEXIST;
    struct nas_tmp_file *inode=tmp_create_at(parent,name,false,0777U);
    if(!inode)return -ENOSPC;
    inode->mode=0120000U|0777U;
    if(tmp_write_at(inode,0,(const uint8_t *)target,length)!=(int64_t)length) {
        tmp_remove_entry(find_child(parent,name));
        return -ENOMEM;
    }
    return 0;
}
int tmp_symlink(const char *target,const char *path) {
    if(bootfs_find(path) || tmp_find(path))return -EEXIST;
    const void *parent=0;char name[256];
    int rc=legacy_parent(path,&parent,name);
    return rc?rc:tmp_symlink_at(parent,name,target);
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
int tmp_link(const char *oldpath,const char *newpath) {
    if(!tmp_path(oldpath) || !tmp_path(newpath))
        return tmp_path(oldpath)==tmp_path(newpath)?-EROFS:-EXDEV;
    const void *oldparent=0,*newparent=0;
    char oldname[256],newname[256];
    int rc=legacy_parent(oldpath,&oldparent,oldname);
    if(rc)return rc;
    rc=legacy_parent(newpath,&newparent,newname);
    if(rc)return rc;
    if(bootfs_find(newpath))return -EEXIST;
    return tmp_link_at(oldparent,oldname,newparent,newname);
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
        char absolute[256];
        if(!dentry_path(d,absolute))return -ENAMETOOLONG;
        for(unsigned n=0;n<NAS_TASK_LIMIT;n++) {
            struct nas_task *task=nas_task_at(n);
            if(task && task->state!=NAS_TASK_FREE && task->fs &&
               !strcmp(task->fs->cwd,absolute))return -EBUSY;
        }
    }
    tmp_remove_entry(d);
    return 0;
}
int tmp_unlink(const char *path,uint64_t flags) {
    const void *parent=0;char name[256];
    int rc=legacy_parent(path,&parent,name);
    return rc?rc:tmp_unlink_at(parent,name,flags);
}
static bool tmp_under(const char *path,const char *dir) {
    size_t n=strlen(dir);
    return !strncmp(path,dir,n) && path[n]=='/';
}
static bool moved_name(char target[256],const char *oldbase,
                       const char *newbase,const char *existing) {
    size_t prefix=strlen(oldbase),destination=strlen(newbase);
    size_t suffix=strlen(existing+prefix);
    if(destination+suffix>=256)return false;
    memcpy(target,newbase,destination);
    memcpy(target+destination,existing+prefix,suffix+1);
    return true;
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
    char oldpath[256],newpath[256],new_parent_path[256];
    if(!dentry_path(source,oldpath))return -ENAMETOOLONG;
    if(new_parent==tmp_root_inode())memcpy(new_parent_path,"/tmp",5);
    else {
        const struct nas_tmp_dentry *p=find_inode(new_parent);
        if(!p || !dentry_path(p,new_parent_path))return -ESTALE;
    }
    size_t prefix=strlen(new_parent_path),leaf=strlen(new_name);
    if(prefix+1+leaf>=256)return -ENAMETOOLONG;
    memcpy(newpath,new_parent_path,prefix);
    newpath[prefix++]='/';
    memcpy(newpath+prefix,new_name,leaf+1);
    /* Preflight path limits for ALL descendants; no mutation can leave a
     * directory tree whose pathname cannot be represented by namei. */
    if(inode->is_dir) {
        char full[256];
        for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
            for(unsigned n=0;n<TMP_DENTRIES_PER_SLAB;n++) {
                const struct nas_tmp_dentry *d=&s->entries[n];
                if(!d->used || !under_inode(d->parent_inode,inode))continue;
                if(!dentry_path(d,full))return -ESTALE;
                if(strlen(newpath)+strlen(full)-strlen(oldpath)>=256)
                    return -ENAMETOOLONG;
            }
        /* Legacy Task.cwd is an ABI string cache, not namespace identity.
         * Keep it in sync; owned cwd_node itself never changes identity. */
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            const struct nas_task *t=nas_task_at(i);
            if(t && t->state!=NAS_TASK_FREE && t->fs &&
               tmp_under(t->fs->cwd,oldpath)) {
                char candidate[256];
                if(!moved_name(candidate,oldpath,newpath,t->fs->cwd))
                    return -ENAMETOOLONG;
            }
        }
    }
    if(target)tmp_remove_entry(target); /* open replaced File retains inode */
    source->parent_inode=new_parent;
    memcpy(source->name,new_name,leaf+1); /* ONE dentry edge is renamed */
    tmp_inode_touch(inode);
    if(inode->is_dir) {
        char candidate[256];
        for(unsigned i=0;i<NAS_TASK_LIMIT;i++) {
            struct nas_task *t=nas_task_at(i);
            if(t && t->state!=NAS_TASK_FREE && t->fs &&
               tmp_under(t->fs->cwd,oldpath)) {
                if(!moved_name(candidate,oldpath,newpath,t->fs->cwd))
                    return -EIO; /* preflight guaranteed */
                memcpy(t->fs->cwd,candidate,strlen(candidate)+1);
            }
        }
    }
    return 0;
}
int tmp_rename(const char *oldpath,const char *newpath,uint64_t flags) {
    if(!tmp_path(oldpath) || !tmp_path(newpath))
        return tmp_path(oldpath)==tmp_path(newpath)?-EROFS:-EXDEV;
    const void *oldparent=0,*newparent=0;
    char oldname[256],newname[256];
    int rc=legacy_parent(oldpath,&oldparent,oldname);
    if(rc)return rc;
    rc=legacy_parent(newpath,&newparent,newname);
    if(rc)return rc;
    if(bootfs_find(newpath))return -EROFS;
    return tmp_rename_at(oldparent,oldname,newparent,newname,flags);
}
