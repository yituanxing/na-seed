#include "lookup.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../mm/heap.h"
bool nas_fs_root_only(const char *path) {
    if(!path || path[0]!='/')return false;
    for(size_t i=1;path[i];i++)if(path[i]!='/')return false;
    return true;
}
/* The only checked provider-local child-name builder. A provider may
 * resolve a live parent inode to its backend spelling; no provider should
 * implement a second copy of leaf component validation. */
int nas_vfs_join_child(const char *parent,const char *name,char out[256]) {
    if(!parent || !name || !out || parent[0]!='/' || !name[0] ||
       strchr(name,'/') || !strcmp(name,".") || !strcmp(name,".."))
        return -EINVAL;
    size_t n=strlen(parent),m=strlen(name);
    if(!n || n>=256 || (n>1 && parent[n-1]=='/'))return -EINVAL;
    if(n+(n>1?1U:0U)+m>=256)return -ENAMETOOLONG;
    memcpy(out,parent,n);
    if(n>1)out[n++]='/';
    memcpy(out+n,name,m+1);
    return 0;
}
/* Compatibility wrappers retain the historical 256-byte canonical result,
 * but all component, dot, symlink and mount semantics now live in ONE
 * Path-first walker below. The string is a projection of an owned Path,
 * never an input used to retraverse cwd/dirfd or select a backend. */
static int legacy_join_parent(const struct nas_vfs_path *parent,
                              const char *leaf,char canonical[256]) {
    char absolute[256];
    int rc=nas_vfs_path_string(parent,absolute);
    return rc ? rc : nas_vfs_join_child(absolute,leaf,canonical);
}
int nas_fs_lookup_node(const char *path,bool follow_final,
                       struct nas_fs_node *result,char resolved[256]) {
    if(!path || !result || !resolved || path[0]!='/')return -EINVAL;
    struct nas_vfs_path owned={0};
    int rc=nas_fs_lookup_path_at(NULL,NULL,path,follow_final,&owned);
    if(rc)return rc;
    rc=nas_vfs_path_node(&owned,result);
    if(!rc)rc=nas_vfs_path_string(&owned,resolved);
    int dropped=nas_vfs_path_put(&owned);
    return dropped ? dropped : rc;
}
int nas_fs_resolve_existing(const char *path,bool follow_final,char out[256]) {
    if(!path || !out || path[0]!='/')return -EINVAL;
    struct nas_vfs_path owned={0};
    int rc=nas_fs_lookup_path_at(NULL,NULL,path,follow_final,&owned);
    if(rc)return rc;
    rc=nas_vfs_path_string(&owned,out);
    int dropped=nas_vfs_path_put(&owned);
    return dropped ? dropped : rc;
}
int nas_fs_lookup_parent(const char *absolute,struct nas_fs_node *parent,
                         char canonical[256]) {
    if(!absolute || !parent || !canonical || absolute[0]!='/')
        return -EINVAL;
    size_t length=strlen(absolute);
    if(length>=256)return -ENAMETOOLONG;
    if(length>1 && absolute[length-1]=='/')return -ENOTDIR;
    struct nas_vfs_path owned={0};
    char leaf[256];
    int rc=nas_fs_lookup_parent_at(NULL,NULL,absolute,&owned,leaf);
    if(rc)return rc;
    rc=nas_vfs_path_node(&owned,parent);
    if(!rc)rc=legacy_join_parent(&owned,leaf,canonical);
    int dropped=nas_vfs_path_put(&owned);
    return dropped ? dropped : rc;
}
int nas_fs_canonical_candidate(const char *absolute,char canonical[256]) {
    if(!absolute || !canonical || absolute[0]!='/')return -EINVAL;
    struct nas_vfs_path existing={0};
    int rc=nas_fs_lookup_path_at(NULL,NULL,absolute,false,&existing);
    if(!rc) {
        rc=nas_vfs_path_string(&existing,canonical);
        int dropped=nas_vfs_path_put(&existing);
        return dropped ? dropped : rc;
    }
    if(rc!=-ENOENT)return rc;
    struct nas_vfs_path parent={0};
    char leaf[256];
    /* Historical mkdir -p allows a missing final directory with '/'. */
    rc=nas_fs_lookup_parent_mkdir_at(NULL,NULL,absolute,&parent,leaf);
    if(rc)return rc;
    rc=legacy_join_parent(&parent,leaf,canonical);
    int dropped=nas_vfs_path_put(&parent);
    return dropped ? dropped : rc;
}

/* Path-first namei: the pathname is only the sequence of names to consume,
 * never the authority for cwd, dirfd, mount attachment or '..'.  A dentry
 * reference owns each intermediate position; an already owned start path
 * therefore cannot be redirected by renaming its former absolute spelling.
 *
 * Keep short paths inline. A symlink expansion or long pathname may allocate
 * heap scratch, but never grows the bounded 16 KiB kernel stack to PATH_MAX.
 * The optional negative-final mode returns a pinned parent and final name
 * only after following a dangling final symlink through the same walker. */
static int walk_path_at(const struct nas_vfs_path *start,
                        const struct nas_vfs_path *root_clamp,
                        const char *path,bool follow_final,bool negative_final,
                        struct nas_vfs_path *out,
                        struct nas_vfs_path *missing_parent,
                        char missing_leaf[256]) {
    if(!path || (!out && !negative_final) ||
       (negative_final && (!missing_parent || !missing_leaf)) ||
       (out && (out==start || out==root_clamp)) ||
       (missing_parent && (missing_parent==start ||
                           missing_parent==root_clamp)))return -EINVAL;
    if(out)*out=(struct nas_vfs_path){0};
    if(missing_parent)*missing_parent=(struct nas_vfs_path){0};
    size_t size=0;
    while(size<NAS_VFS_WALK_PATH_LIMIT && path[size])++size;
    if(size>=NAS_VFS_WALK_PATH_LIMIT)return -ENAMETOOLONG;
    if(!size)return -ENOENT;
    char short_pending[256];
    size_t capacity=sizeof(short_pending);
    char *pending=short_pending;
    if(size>=capacity) {
        capacity=NAS_VFS_WALK_PATH_LIMIT;
        pending=nas_heap_alloc(capacity);
        if(!pending)return -ENOMEM;
    }
    memcpy(pending,path,size+1);
    bool trailing_slash=size>1 && pending[size-1]=='/';
    struct nas_vfs_path current={0};
    int rc=0;
    if(path[0]=='/') {
        rc=root_clamp && root_clamp->dentry ?
            nas_vfs_path_get(root_clamp,&current) :
            nas_vfs_path_root(&current);
        /* CPIO-less direct-ELF boot can enter '/mnt' through its first
         * component. Only a genuinely absent '/' permits that bootstrap. */
        if(rc && rc!=-ENOENT)goto done;
        if(rc==-ENOENT)rc=0;
    } else {
        if(!start || !start->dentry) {rc=-ENOENT;goto done;}
        rc=nas_vfs_path_get(start,&current);
        if(rc)goto done;
    }
    unsigned links=0;
    size_t pos=0;
    for(;;) {
        while(pending[pos]=='/')++pos;
        if(!pending[pos])break;
        size_t begin=pos;
        while(pending[pos] && pending[pos]!='/')++pos;
        size_t len=pos-begin;
        if(len>=256) {rc=-ENAMETOOLONG;goto done;}
        char name[256];
        memcpy(name,pending+begin,len);
        name[len]=0;
        size_t tail=pos;
        while(pending[tail]=='/')++tail;
        bool final=!pending[tail];
        if(!strcmp(name,".") || !strcmp(name,"..")) {
            struct nas_fs_node node;
            if(!current.dentry) {rc=-ENOENT;goto done;}
            rc=nas_vfs_path_node(&current,&node);
            if(rc)goto done;
            if(!node.is_dir) {rc=-ENOTDIR;goto done;}
            if(name[1]=='.') {
                struct nas_vfs_path next={0};
                rc=nas_vfs_path_parent(&current,root_clamp,&next);
                if(rc)goto done;
                int dropped=nas_vfs_path_put(&current);
                current=next;
                if(dropped) {rc=dropped;goto done;}
            }
            continue;
        }
        struct nas_vfs_path next={0};
        rc=nas_vfs_path_lookup_child(current.dentry?&current:NULL,
                                     name,&next);
        if(rc==-ENOENT && negative_final && final && !trailing_slash &&
           current.dentry) {
            *missing_parent=current;
            current=(struct nas_vfs_path){0};
            memcpy(missing_leaf,name,len+1);
            rc=0;
            goto done;
        }
        if(rc)goto done;
        struct nas_fs_node node;
        rc=nas_vfs_path_node(&next,&node);
        if(rc) {(void)nas_vfs_path_put(&next);goto done;}
        if(node.is_symlink && (!final || follow_final || trailing_slash)) {
            if(++links>8) {
                (void)nas_vfs_path_put(&next);
                rc=-ELOOP;goto done;
            }
            char target[256];
            int n=nas_vfs_readlink(&node,target);
            (void)nas_vfs_path_put(&next);
            if(n<0) {rc=n;goto done;}
            if(!n) {rc=-ENOENT;goto done;}
            if(n>=256) {rc=-ENAMETOOLONG;goto done;}
            for(int i=0;i<n;i++)if(!target[i]) {
                rc=-EINVAL;goto done;
            }
            size_t remaining=strlen(pending+pos);
            size_t next_size=(size_t)n+remaining;
            if(next_size>=NAS_VFS_WALK_PATH_LIMIT) {
                rc=-ENAMETOOLONG;goto done;
            }
            if(next_size+1>capacity) {
                char *larger=nas_heap_alloc(NAS_VFS_WALK_PATH_LIMIT);
                if(!larger) {rc=-ENOMEM;goto done;}
                memcpy(larger,pending,size+1);
                if(pending!=short_pending)(void)nas_heap_free(pending);
                pending=larger;
                capacity=NAS_VFS_WALK_PATH_LIMIT;
            }
            memmove(pending+n,pending+pos,remaining+1);
            memcpy(pending,target,(size_t)n);
            size=next_size;
            pos=0;
            trailing_slash=size>1 && pending[size-1]=='/';
            if(target[0]=='/') {
                if(current.dentry) {
                    int dropped=nas_vfs_path_put(&current);
                    if(dropped) {rc=dropped;goto done;}
                }
                rc=root_clamp && root_clamp->dentry ?
                   nas_vfs_path_get(root_clamp,&current) :
                   nas_vfs_path_root(&current);
                if(rc && rc!=-ENOENT)goto done;
                if(rc==-ENOENT)rc=0;
            }
            continue;
        }
        if((!final || trailing_slash) && !node.is_dir) {
            (void)nas_vfs_path_put(&next);
            rc=-ENOTDIR;goto done;
        }
        if(current.dentry) {
            int dropped=nas_vfs_path_put(&current);
            if(dropped) {
                (void)nas_vfs_path_put(&next);
                rc=dropped;goto done;
            }
        }
        current=next;
    }
    if(!current.dentry) {rc=-ENOENT;goto done;}
    if(trailing_slash) {
        struct nas_fs_node node;
        rc=nas_vfs_path_node(&current,&node);
        if(rc)goto done;
        if(!node.is_dir) {rc=-ENOTDIR;goto done;}
    }
    if(negative_final) {rc=-EEXIST;goto done;}
    *out=current;
    current=(struct nas_vfs_path){0};
    rc=0;
done:
    if(current.dentry)(void)nas_vfs_path_put(&current);
    if(pending!=short_pending)(void)nas_heap_free(pending);
    return rc;
}
int nas_fs_lookup_path_at(const struct nas_vfs_path *start,
                          const struct nas_vfs_path *root_clamp,
                          const char *path,bool follow_final,
                          struct nas_vfs_path *out) {
    return walk_path_at(start,root_clamp,path,follow_final,false,
                        out,NULL,NULL);
}
int nas_fs_resolve_create_parent_at(const struct nas_vfs_path *start,
                                    const struct nas_vfs_path *root_clamp,
                                    const char *path,
                                    struct nas_vfs_path *parent,
                                    char leaf[256]) {
    return walk_path_at(start,root_clamp,path,true,true,
                        NULL,parent,leaf);
}
static int lookup_parent_at_mode(const struct nas_vfs_path *start,
                                 const struct nas_vfs_path *root_clamp,
                                 const char *path,struct nas_vfs_path *parent,
                                 char leaf[256],
                                 enum nas_fs_parent_trailing_policy policy) {
    if(!path || !parent || !leaf || parent==start ||
       parent==root_clamp)return -EINVAL;
    *parent=(struct nas_vfs_path){0};
    size_t n=0;
    while(n<NAS_VFS_WALK_PATH_LIMIT && path[n])++n;
    if(n>=NAS_VFS_WALK_PATH_LIMIT)return -ENAMETOOLONG;
    if(!n)return -ENOENT;
    if(policy>NAS_FS_TRAILING_RENAME_DEST)return -EINVAL;
    if(n>1 && path[n-1]=='/' && policy!=NAS_FS_TRAILING_MKDIR) {
        /* Mutation targets carrying a trailing slash require that the
         * ACTUAL final named entry is a directory, not merely a symlink
         * which follows to one.  Otherwise unlink(link_to_dir/) would
         * delete the symlink after this helper removes the slash. */
        size_t trimmed=n;
        while(trimmed>1 && path[trimmed-1]=='/')--trimmed;
        char inline_target[256];
        char *target=inline_target;
        if(trimmed>=sizeof(inline_target)) {
            target=nas_heap_alloc(trimmed+1);
            if(!target)return -ENOMEM;
        }
        memcpy(target,path,trimmed);
        target[trimmed]=0;
        struct nas_vfs_path existing={0};
        int trailing_rc=nas_fs_lookup_path_at(start,root_clamp,target,false,
                                              &existing);
        if(!trailing_rc) {
            struct nas_fs_node inode;
            trailing_rc=nas_vfs_path_node(&existing,&inode);
            if(!trailing_rc && policy!=NAS_FS_TRAILING_CREATE_DEST &&
               (!inode.is_dir || inode.is_symlink))
                trailing_rc=-ENOTDIR;
            (void)nas_vfs_path_put(&existing);
        }
        if(target!=inline_target)(void)nas_heap_free(target);
        if(trailing_rc==-ENOENT && policy==NAS_FS_TRAILING_RENAME_DEST)
            trailing_rc=-ENOTDIR;
        if(trailing_rc)return trailing_rc;
    }
    while(n>1 && path[n-1]=='/')--n;
    if(n==1 && path[0]=='/')
        return (policy==NAS_FS_TRAILING_MKDIR ||
                policy==NAS_FS_TRAILING_CREATE_DEST) ? -EEXIST : -EBUSY;
    size_t leaf_start=n;
    while(leaf_start>0 && path[leaf_start-1]!='/')--leaf_start;
    size_t length=n-leaf_start;
    if(!length || length>=256)return -ENAMETOOLONG;
    memcpy(leaf,path+leaf_start,length);
    leaf[length]=0;
    if(!strcmp(leaf,".") || !strcmp(leaf,"..")) {
        /* mkdir(".") and mkdir("..") address existing directories.
         * Validate the complete spelling through the normal namei walker:
         * missing or non-directory prefixes must still report their true
         * ENOENT/ENOTDIR (and symlink loops retain ELOOP).  Other namespace
         * mutations must never forward dot components to a provider. */
        if(policy!=NAS_FS_TRAILING_MKDIR)return -EINVAL;
        struct nas_vfs_path existing={0};
        int rc=nas_fs_lookup_path_at(start,root_clamp,path,true,&existing);
        if(rc)return rc;
        (void)nas_vfs_path_put(&existing);
        return -EEXIST;
    }
    if(!leaf_start) {
        if(!start || !start->dentry)return -ENOENT;
        int rc=nas_vfs_path_get(start,parent);
        if(rc)return rc;
    } else {
        size_t base_length=leaf_start==1?1:leaf_start-1;
        char local_base[256];
        char *base=local_base;
        if(base_length>=sizeof(local_base)) {
            base=nas_heap_alloc(base_length+1);
            if(!base)return -ENOMEM;
        }
        memcpy(base,path,base_length);
        base[base_length]=0;
        int rc=nas_fs_lookup_path_at(start,root_clamp,base,true,parent);
        if(base!=local_base)(void)nas_heap_free(base);
        if(rc)return rc;
    }
    struct nas_fs_node inode;
    int rc=nas_vfs_path_node(parent,&inode);
    if(!rc && !inode.is_dir)rc=-ENOTDIR;
    if(rc) {
        (void)nas_vfs_path_put(parent);
        return rc;
    }
    return 0;
}
int nas_fs_lookup_parent_at(const struct nas_vfs_path *start,
                            const struct nas_vfs_path *root_clamp,
                            const char *path,struct nas_vfs_path *parent,
                            char leaf[256]) {
    return lookup_parent_at_mode(start,root_clamp,path,parent,leaf,
                                 NAS_FS_TRAILING_STRICT);
}
int nas_fs_lookup_parent_at_policy(const struct nas_vfs_path *start,
                                   const struct nas_vfs_path *root_clamp,
                                   const char *path,
                                   enum nas_fs_parent_trailing_policy policy,
                                   struct nas_vfs_path *parent,
                                   char leaf[256]) {
    return lookup_parent_at_mode(start,root_clamp,path,parent,leaf,policy);
}
int nas_fs_lookup_parent_mkdir_at(const struct nas_vfs_path *start,
                                  const struct nas_vfs_path *root_clamp,
                                  const char *path,struct nas_vfs_path *parent,
                                  char leaf[256]) {
    return lookup_parent_at_mode(start,root_clamp,path,parent,leaf,
                                 NAS_FS_TRAILING_MKDIR);
}
