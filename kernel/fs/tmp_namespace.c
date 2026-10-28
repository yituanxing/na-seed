#include "../tmpfs.h"
#include "../task.h"
#include "../bootfs.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../mm/frame.h"
#include "../arch/arch.h"

/* Writable /tmp namespace: dentry names are distinct from sparse inode
 * identity, open file descriptions and CPIO. One page holds many reusable
 * dentries. This is NOT a global Linux mount/dcache implementation. */
#define TMP_DENTRIES_PER_SLAB ((4096U - sizeof(void *)) / sizeof(struct nas_tmp_dentry))
struct tmp_dentry_slab {
    struct tmp_dentry_slab *next;
    struct nas_tmp_dentry entries[TMP_DENTRIES_PER_SLAB];
};
_Static_assert(TMP_DENTRIES_PER_SLAB>=2, "dentries fit a frame");
_Static_assert(sizeof(struct tmp_dentry_slab)<=4096, "dentry slab too large");
static struct tmp_dentry_slab *tmp_dentry_slabs;

static struct nas_tmp_dentry *tmp_find_entry(const char *path) {
    if(!path)return 0;
    for(struct tmp_dentry_slab *slab=tmp_dentry_slabs;slab;slab=slab->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++) {
            struct nas_tmp_dentry *d=&slab->entries[i];
            if(d->used && strcmp(d->path,path)==0)return d;
        }
    return 0;
}
static struct nas_tmp_dentry *tmp_insert(const char *path,
                                         struct nas_tmp_file *inode) {
    if(!path || !inode || !inode->used || tmp_find_entry(path))return 0;
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
    unsigned n=0;
    while(path[n]) {if(n==255)return 0;entry->path[n]=path[n];n++;}
    entry->path[n]=0;
    entry->inode=inode;
    entry->used=true;
    tmp_inode_link(inode);
    return entry;
}
static void tmp_remove_entry(struct nas_tmp_dentry *d) {
    if(!d || !d->used)return;
    struct nas_tmp_file *inode=d->inode;
    d->used=false;d->inode=0;d->path[0]=0;
    tmp_inode_unlink(inode); /* final name no longer implies final close */
}
bool tmp_path(const char *path) {
    return path && path[0]=='/' && path[1]=='t' && path[2]=='m' &&
           path[3]=='p' && path[4]=='/' && path[5];
}
bool tmp_parent(const char *path,char parent[256]) {
    if(!tmp_path(path))return false;
    unsigned n=0,slash=0;
    for(;path[n] && n<255;n++)if(path[n]=='/')slash=n;
    if(path[n] || slash<4 || !path[slash+1])return false;
    if(slash==4) {
        parent[0]='/';parent[1]='t';parent[2]='m';parent[3]='p';parent[4]=0;
    } else {
        for(unsigned i=0;i<slash;i++)parent[i]=path[i];
        parent[slash]=0;
    }
    return true;
}
struct nas_tmp_file *tmp_find(const char *path) {
    struct nas_tmp_dentry *d=tmp_find_entry(path);
    return d ? d->inode : 0;
}
struct nas_tmp_file *tmp_find_child(const void *parent_inode,const char *name) {
    if(!parent_inode || !name || !name[0] || strchr(name,'/'))return 0;
    const struct bootfs_entry *root=bootfs_find("/tmp");
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++) {
            struct nas_tmp_dentry *d=&s->entries[i];
            if(!d->used)continue;
            const char *leaf=strrchr(d->path,'/');
            if(!leaf || strcmp(leaf+1,name))continue;
            char parent_path[256];
            if(!tmp_parent(d->path,parent_path))continue;
            if(!strcmp(parent_path,"/tmp")) {
                if(root && parent_inode==(const void *)root)return d->inode;
            } else if(parent_inode==(const void *)tmp_find(parent_path))
                return d->inode;
        }
    return 0;
}
const char *tmp_name(const struct nas_tmp_file *file) {
    if(!file || !file->used)return 0;
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++)
            if(s->entries[i].used && s->entries[i].inode==file)
                return s->entries[i].path;
    return 0;
}
const struct nas_tmp_dentry *tmp_child_entry(const char *parent,unsigned index) {
    if(!parent)return 0;
    for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned i=0;i<TMP_DENTRIES_PER_SLAB;i++) {
            const struct nas_tmp_dentry *d=&s->entries[i];
            if(!d->used)continue;
            char dir[256];
            if(!tmp_parent(d->path,dir) || strcmp(dir,parent)!=0)continue;
            if(!index)return d;
            index--;
        }
    return 0;
}
int tmp_parent_check(const char *path) {
    char parent[256];
    if(!tmp_parent(path,parent))return -EROFS;
    if(strcmp(parent,"/tmp")==0)
        return bootfs_is_dir(bootfs_find("/tmp"))?0:-ENOENT;
    struct nas_tmp_file *dir=tmp_find(parent);
    return !dir?-ENOENT:dir->is_dir?0:-ENOTDIR;
}
struct nas_tmp_file *tmp_create(const char *path,bool dir,uint32_t mode) {
    if(!path || tmp_find_entry(path))return 0;
    struct nas_tmp_file *inode=tmp_inode_alloc(dir,mode);
    if(!inode)return 0;
    if(!tmp_insert(path,inode)) {
        tmp_inode_discard(inode);
        return 0;
    }
    return inode;
}
int tmp_symlink(const char *target,const char *path) {
    if(!target || !target[0])return -ENOENT;
    unsigned length=0;
    while(target[length])if(++length>=256)return -ENAMETOOLONG;
    if(bootfs_find(path) || tmp_find(path))return -EEXIST;
    if(!tmp_path(path) || !bootfs_mounted())return -EROFS;
    int rc=tmp_parent_check(path);
    if(rc)return rc;
    struct nas_tmp_file *inode=tmp_create(path,false,0777U);
    if(!inode)return -ENOSPC;
    inode->mode=0120000U|0777U;
    if(tmp_write_at(inode,0,(const uint8_t *)target,length)!=(int64_t)length) {
        tmp_remove_entry(tmp_find_entry(path));
        return -ENOMEM;
    }
    return 0;
}
int tmp_mkdir(const char *path,uint64_t mode) {
    if(bootfs_find(path) || tmp_find(path))return -EEXIST;
    if(!tmp_path(path) || !bootfs_mounted())return -EROFS;
    int parent=tmp_parent_check(path);
    if(parent)return parent;
    return tmp_create(path,true,(uint32_t)mode)?0:-ENOSPC;
}
int tmp_link(const char *oldpath,const char *newpath) {
    if(!tmp_path(oldpath) || !tmp_path(newpath))
        return tmp_path(oldpath)==tmp_path(newpath)?-EROFS:-EXDEV;
    if(!bootfs_mounted())return -EROFS;
    struct nas_tmp_file *inode=tmp_find(oldpath);
    if(!inode)return bootfs_find(oldpath)?-EXDEV:-ENOENT;
    if(inode->is_dir)return -EPERM; /* forbid directory hard links */
    if(bootfs_find(newpath) || tmp_find(newpath))return -EEXIST;
    int rc=tmp_parent_check(newpath);
    if(rc)return rc;
    if(inode->nlink==UINT32_MAX)return -EMLINK;
    if(!tmp_insert(newpath,inode))return -ENOSPC;
    return 0;
}
int tmp_unlink(const char *path,uint64_t flags) {
    const uint64_t removedir=0x200ULL;
    if(flags & ~removedir)return -EINVAL;
    if(!tmp_path(path) || !bootfs_mounted())return -EROFS;
    struct nas_tmp_dentry *d=tmp_find_entry(path);
    if(!d) {
        int rc=tmp_parent_check(path);
        return rc?rc:-ENOENT;
    }
    struct nas_tmp_file *inode=d->inode;
    if(inode->is_dir && !(flags&removedir))return -EISDIR;
    if(!inode->is_dir && (flags&removedir))return -ENOTDIR;
    if(inode->is_dir) {
        if(tmp_child_entry(path,0))return -ENOTEMPTY;
        for(unsigned n=0;n<NAS_TASK_LIMIT;n++) {
            struct nas_task *t=nas_task_at(n);
            if(t && t->state!=NAS_TASK_FREE && strcmp(t->fs->cwd,path)==0)
                return -EBUSY;
        }
    }
    tmp_remove_entry(d);
    return 0;
}
static bool tmp_under(const char *path,const char *directory) {
    unsigned n=0;
    while(directory[n]){if(path[n]!=directory[n])return false;n++;}
    return path[n]=='/';
}
static bool tmp_move_name(char dest[256],const char *newbase,
                          const char *oldbase,const char *existing) {
    const unsigned base=strlen(newbase),cut=strlen(oldbase);
    const unsigned suffix=strlen(existing+cut);
    if(base+suffix>=256)return false;
    for(unsigned i=0;i<base;i++)dest[i]=newbase[i];
    for(unsigned i=0;i<=suffix;i++)dest[base+i]=existing[cut+i];
    return true;
}
int tmp_rename(const char *oldpath,const char *newpath,uint64_t flags) {
    if(flags&~1ULL)return -EINVAL;
    const bool oldtmp=tmp_path(oldpath),newtmp=tmp_path(newpath);
    if(oldtmp!=newtmp)return -EXDEV;
    if(!oldtmp || !bootfs_mounted())return -EROFS;
    struct nas_tmp_dentry *source=tmp_find_entry(oldpath);
    if(!source) {
        int parent=tmp_parent_check(oldpath);
        return parent?parent:-ENOENT;
    }
    if(strcmp(oldpath,newpath)==0)return 0;
    int parent=tmp_parent_check(newpath);
    if(parent)return parent;
    struct nas_tmp_file *inode=source->inode;
    if(inode->is_dir && tmp_under(newpath,oldpath))return -EINVAL;
    struct nas_tmp_dentry *target=tmp_find_entry(newpath);
    if(target) {
        if(flags&1ULL)return -EEXIST;
        if(target->inode==inode)return 0; /* POSIX rename same inode no-op */
        if(inode->is_dir && !target->inode->is_dir)return -ENOTDIR;
        if(!inode->is_dir && target->inode->is_dir)return -EISDIR;
        if(target->inode->is_dir && tmp_child_entry(newpath,0))
            return -ENOTEMPTY;
    }
    /* Preflight entire subtree and all task cwd strings before changes. */
    if(inode->is_dir) {
        char candidate[256];
        for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned n=0;n<TMP_DENTRIES_PER_SLAB;n++) {
            const struct nas_tmp_dentry *e=&s->entries[n];
            if(e->used && tmp_under(e->path,oldpath) &&
               !tmp_move_name(candidate,newpath,oldpath,e->path))
                return -ENAMETOOLONG;
        }
        for(unsigned n=0;n<NAS_TASK_LIMIT;n++) {
            const struct nas_task *t=nas_task_at(n);
            if(t && t->state!=NAS_TASK_FREE &&
               tmp_under(t->fs->cwd,oldpath) &&
               !tmp_move_name(candidate,newpath,oldpath,t->fs->cwd))
                return -ENAMETOOLONG;
        }
    }
    if(target)tmp_remove_entry(target); /* preserve open replaced inode */
    if(inode->is_dir) {
        char candidate[256];
        for(struct tmp_dentry_slab *s=tmp_dentry_slabs;s;s=s->next)
        for(unsigned n=0;n<TMP_DENTRIES_PER_SLAB;n++) {
            struct nas_tmp_dentry *e=&s->entries[n];
            if(!e->used || !tmp_under(e->path,oldpath))continue;
            if(!tmp_move_name(candidate,newpath,oldpath,e->path))
                return -ENAMETOOLONG; /* preflight guarantees */
            unsigned i=0;
            do{e->path[i]=candidate[i];}while(candidate[i++]);
        }
        for(unsigned n=0;n<NAS_TASK_LIMIT;n++) {
            struct nas_task *t=nas_task_at(n);
            if(!t || t->state==NAS_TASK_FREE ||
               !tmp_under(t->fs->cwd,oldpath))continue;
            if(!tmp_move_name(candidate,newpath,oldpath,t->fs->cwd))
                return -ENAMETOOLONG;
            unsigned i=0;
            do{t->fs->cwd[i]=candidate[i];}while(candidate[i++]);
        }
    }
    unsigned i=0;
    do{source->path[i]=newpath[i];}while(newpath[i++]);
    tmp_inode_touch(inode);
    return 0;
}
