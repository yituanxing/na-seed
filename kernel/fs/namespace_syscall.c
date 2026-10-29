/* Pathname and namespace Linux ABI adapter. No filesystem-specific
 * mutation dispatch: VFS selects the real parent mount and its provider. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../panic.h"
#include "../task.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../path.h"
#include "../arch/linux_fcntl_abi.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/mmu.h"
#include "mode.h"
#include "syscall.h"
#include "lookup.h"
#include "vfs.h"
#include "procfs.h"
#include "namespace_syscall.h"
#include "path_request.h"
#define TASK (*nas_current_task)
#define current_workdir TASK.fs->cwd

void nas_fs_path_end(struct nas_fs_path_request *request) {
    if(!request)return;
    if(request->start.dentry && nas_vfs_path_put(&request->start))
        nas_fail("path request start reference corrupted");
    if(request->root.dentry && nas_vfs_path_put(&request->root))
        nas_fail("path request root reference corrupted");
}

int nas_fs_kernel_path_begin(uint64_t dirfd,const char *kernel_path,
                             struct nas_fs_path_request *request) {
    if(!kernel_path || !request)return -EINVAL;
    *request=(struct nas_fs_path_request){0};
    size_t length=strnlen(kernel_path,sizeof(request->pathname));
    if(length==sizeof(request->pathname))return -ENAMETOOLONG;
    if(!length)return -ENOENT;
    memcpy(request->pathname,kernel_path,length+1);
    int rc=nas_vfs_path_root(&request->root);
    /* Original direct-ELF boots can have mounted children but no root
     * archive. Path namei has a rootless fallback; keep a NULL clamp only
     * for this precise case, never for a failed mounted lookup. */
    if(rc==-ENOENT)rc=0;
    else if(rc)return rc;
    if(kernel_path[0]=='/')return 0;
    int64_t fd=nas_linux_signed_dirfd(dirfd);
    const struct nas_vfs_path *base=NULL;
    if(fd==-100) {
        if(TASK.fs->cwd_path.dentry)base=&TASK.fs->cwd_path;
        else if(TASK.fs->cwd_pinned)rc=-ESTALE;
        else if(request->root.dentry)base=&request->root;
        else rc=-ENOENT;
    } else if(fd<0 || (uint64_t)fd>=NAS_FD_LIMIT ||
              TASK.fds->entries[fd].kind==FD_CLOSED)rc=-EBADF;
    else {
        const struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[fd].slot];
        if(!opened->vnode_pinned || !opened->vnode.is_dir)rc=-ENOTDIR;
        else if(!opened->vpath.dentry)rc=-ESTALE;
        else base=&opened->vpath;
    }
    if(!rc && base)rc=nas_vfs_path_get(base,&request->start);
    if(rc)nas_fs_path_end(request);
    return rc;
}

int nas_fs_user_path_begin(uint64_t dirfd,uint64_t user_pointer,
                           struct nas_fs_path_request *request) {
    if(!request)return -EINVAL;
    if(user_pointer>=USER_LIMIT)return -EFAULT;
    char source[256];
    unsigned i=0;
    for(;i<sizeof(source);i++) {
        uint8_t *byte=nas_mmu_user_byte(user_pointer+i);
        if(!byte)return -EFAULT;
        source[i]=(char)*byte;
        if(!source[i])break;
    }
    if(i==sizeof(source))return -ENAMETOOLONG;
    return nas_fs_kernel_path_begin(dirfd,source,request);
}

int nas_fs_user_lookup_begin(uint64_t dirfd,uint64_t user_pointer,
                             bool follow_final,
                             struct nas_fs_path_request *request,
                             struct nas_vfs_path *found,
                             struct nas_fs_node *node) {
    if(!request || !found || !node)return -EINVAL;
    *found=(struct nas_vfs_path){0};
    int rc=nas_fs_user_path_begin(dirfd,user_pointer,request);
    if(rc)return rc;
    rc=nas_fs_lookup_path_at(request->start.dentry?&request->start:NULL,
                             request->root.dentry?&request->root:NULL,
                             request->pathname,follow_final,found);
    if(!rc)rc=nas_vfs_path_node(found,node);
    if(rc) {
        if(found->dentry && nas_vfs_path_put(found))
            nas_fail("path lookup result reference corrupted");
        nas_fs_path_end(request);
    }
    return rc;
}

/* Component walking, dot/dotdot, symlinks and negative final names
 * belong to one namei owner in lookup.c, not syscall-specific copies. */
static int resolve_component_path(const char *base,const char *src,
                                  char out[256]) {
    char raw[256];
    int rc=nas_path_join_raw(base,src,raw);
    if(rc)return rc;
    rc=nas_fs_canonical_candidate(raw,out);
    if(rc!=-ENOENT)return rc;
    /* Temporary bootstrap compatibility: synthetic /dev names have no
     * devfs inodes, and CPIO-less direct ELF has no root VFS inode.
     * Never skip actual dot components via this escape hatch. */
    bool dot=false;
    for(unsigned i=1;raw[i];i++) {
        if(raw[i]!='.' || raw[i-1]!='/')continue;
        if(!raw[i+1] || raw[i+1]=='/' ||
           (raw[i+1]=='.' && (!raw[i+2] || raw[i+2]=='/')))
            dot=true;
    }
    if(!dot && (!strncmp(raw,"/dev/",5) || !bootfs_mounted())) {
        memcpy(out,raw,strlen(raw)+1);
        return 0;
    }
    return rc;
}
/* This adapter owns user-memory copying and directory-FD resolution.
 * Component semantics are checked using the same VFS namei as openat. */
uint64_t nas_fs_resolve_path(uint64_t dirfd, uint64_t pathname,
                             char out[256]) {
    char src[256];
    unsigned n=0;
    if (pathname>=USER_LIMIT) return (uint64_t)-EFAULT;
    for (;n<sizeof(src);n++) {
        uint8_t *p=nas_mmu_user_byte(pathname+n);
        if (!p) return (uint64_t)-EFAULT;
        src[n]=(char)*p;
        if (!src[n]) break;
    }
    if (n==sizeof(src)) return (uint64_t)-ENAMETOOLONG;
    if (!n) return (uint64_t)-ENOENT;
    const char *base=NULL;
    char inode_base[256];
    if(src[0]!='/') {
        /* *at syscall dirfd is a native int, not a native pointer/long.
         * Do not compare raw 64-bit syscall registers against (u64)-100:
         * GNU libc/BusyBox may hand us 0x00000000ffffff9c. */
        int64_t fd=nas_linux_signed_dirfd(dirfd);
        if(fd==-100) {
            if(TASK.fs->cwd_path.dentry) {
                int err=nas_vfs_path_string(&TASK.fs->cwd_path,inode_base);
                if(err)return (uint64_t)(int64_t)err;
                base=inode_base;
            } else if(TASK.fs->cwd_pinned) {
                /* An inode-only FS cwd is an invalid owner, NOT a reason
                 * to reverse-resolve a stale pathname from a provider. */
                return (uint64_t)-ESTALE;
            } else base=current_workdir;
        } else {
            if(fd<0 || (uint64_t)fd>=NAS_FD_LIMIT ||
               TASK.fds->entries[fd].kind==FD_CLOSED)
                return (uint64_t)-EBADF;
            const struct nas_open_file *opened=
                &nas_open_files[TASK.fds->entries[fd].slot];
            /* Linux openat(dirfd) uses the file's owned directory path.
             * No procfs/tmpfs/archive FD type is privileged here. */
            if(!opened->vnode_pinned || !opened->vnode.is_dir)
                return (uint64_t)-ENOTDIR;
            /* Every ordinary directory File owns a Path from the same
             * VFS open transaction. A missing path is a broken owner,
             * NEVER a reason to reopen origin_path through the backend. */
            if(!opened->vpath.dentry)return (uint64_t)-ESTALE;
            int err=nas_vfs_path_string(&opened->vpath,inode_base);
            if(err)return (uint64_t)(int64_t)err;
            base=inode_base;
        }
    }
    return (uint64_t)(int64_t)resolve_component_path(base,src,out);
}

/* Linux syscall decoders retain user-pointer validation and task-relative
 * path resolution; the RAM filesystem backend owns namespace operations. */
static uint64_t sys_mkdirat_tmp(uint64_t dirfd, uint64_t pathname,
                                uint64_t mode) {
    struct nas_fs_path_request path;
    int rc=nas_fs_user_path_begin(dirfd,pathname,&path);
    if(rc)return (uint64_t)(int64_t)rc;
    /* The Linux VFS reports an existing root as EEXIST for mkdir("/").
     * A parent/name lookup has no final leaf for a slash-only spelling. */
    bool root_only=path.pathname[0]=='/';
    for(unsigned i=1;root_only && path.pathname[i];i++)
        if(path.pathname[i]!='/')root_only=false;
    if(root_only) {
        nas_fs_path_end(&path);
        return (uint64_t)-EEXIST;
    }
    struct nas_vfs_path parent={0};char leaf[256];
    rc=nas_fs_lookup_parent_mkdir_at(path.start.dentry?&path.start:NULL,
                                     path.root.dentry?&path.root:NULL,
                                     path.pathname,&parent,leaf);
    if(!rc)rc=nas_vfs_mkdir_child(&parent,leaf,
                            nas_fs_create_mode(mode,TASK.fs->umask));
    if(parent.dentry && nas_vfs_path_put(&parent))nas_fail("mkdir parent ref");
    nas_fs_path_end(&path);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_unlinkat_tmp(uint64_t dirfd, uint64_t pathname,
                                 uint64_t flags) {
    struct nas_fs_path_request path;
    int rc=nas_fs_user_path_begin(dirfd,pathname,&path);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_vfs_path parent={0};char leaf[256];
    rc=nas_fs_lookup_parent_at(path.start.dentry?&path.start:NULL,
                               path.root.dentry?&path.root:NULL,
                               path.pathname,&parent,leaf);
    if(!rc)rc=nas_vfs_unlink_child(&parent,leaf,flags);
    if(parent.dentry && nas_vfs_path_put(&parent))nas_fail("unlink parent ref");
    nas_fs_path_end(&path);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_renameat_tmp(uint64_t olddirfd, uint64_t oldptr,
                                 uint64_t newdirfd, uint64_t newptr,
                                 uint64_t flags) {
    /* Reject unsupported flags before reading user paths, as before. */
    if (flags & ~1ULL) return (uint64_t)-EINVAL;
    struct nas_fs_path_request oldpath,newpath;
    int rc=nas_fs_user_path_begin(olddirfd,oldptr,&oldpath);
    if(rc)return (uint64_t)(int64_t)rc;
    rc=nas_fs_user_path_begin(newdirfd,newptr,&newpath);
    if(rc) {
        nas_fs_path_end(&oldpath);
        return (uint64_t)(int64_t)rc;
    }
    struct nas_vfs_path from={0},to={0};
    char from_leaf[256],to_leaf[256];
    rc=nas_fs_lookup_parent_at(oldpath.start.dentry?&oldpath.start:NULL,
                               oldpath.root.dentry?&oldpath.root:NULL,
                               oldpath.pathname,&from,from_leaf);
    if(!rc)rc=nas_fs_lookup_parent_at_policy(
                       newpath.start.dentry?&newpath.start:NULL,
                       newpath.root.dentry?&newpath.root:NULL,
                       newpath.pathname,NAS_FS_TRAILING_RENAME_DEST,
                       &to,to_leaf);
    if(!rc) {
        size_t new_len=strlen(newpath.pathname);
        /* Only the original spelling's terminal separator is Linux ABI
         * information; VFS alone checks source inode and target identity. */
        bool trailing=new_len && newpath.pathname[new_len-1]=='/';
        rc=nas_vfs_rename_child_with_intent(&from,from_leaf,&to,to_leaf,
                                             flags,trailing);
    }
    if(to.dentry && nas_vfs_path_put(&to))nas_fail("rename new parent ref");
    if(from.dentry && nas_vfs_path_put(&from))nas_fail("rename old parent ref");
    nas_fs_path_end(&newpath);
    nas_fs_path_end(&oldpath);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_symlinkat_tmp(uint64_t targetptr,uint64_t dirfd,
                                  uint64_t linkptr) {
    char target[256];
    if (targetptr>=USER_LIMIT) return (uint64_t)-EFAULT;
    unsigned i=0;
    for (;i<sizeof(target);i++) {
        uint8_t *p=nas_mmu_user_byte(targetptr+i);
        if (!p) return (uint64_t)-EFAULT;
        target[i]=(char)*p;
        if (!target[i]) break;
    }
    if (i==sizeof(target)) return (uint64_t)-ENAMETOOLONG;
    struct nas_fs_path_request link;
    int rc=nas_fs_user_path_begin(dirfd,linkptr,&link);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_vfs_path parent={0};char leaf[256];
    rc=nas_fs_lookup_parent_at_policy(link.start.dentry?&link.start:NULL,
                                      link.root.dentry?&link.root:NULL,
                                      link.pathname,NAS_FS_TRAILING_CREATE_DEST,
                                      &parent,leaf);
    if(!rc)rc=nas_vfs_symlink_child(target,&parent,leaf);
    if(parent.dentry && nas_vfs_path_put(&parent))nas_fail("symlink parent ref");
    nas_fs_path_end(&link);
    return (uint64_t)(int64_t)rc;
}

/* Hardlinks reuse the same tmpfs inode: no copies, no fake CPIO writes.
 * AT_SYMLINK_FOLLOW applies only to the old endpoint of linkat(). */
static uint64_t sys_linkat_tmp(uint64_t olddirfd,uint64_t oldptr,
                               uint64_t newdirfd,uint64_t newptr,
                               uint64_t flags) {
    const uint64_t AT_SYMLINK_FOLLOW=0x400ULL;
    if(flags & ~AT_SYMLINK_FOLLOW)return (uint64_t)-EINVAL;
    struct nas_fs_path_request oldpath,newpath;
    int rc=nas_fs_user_path_begin(olddirfd,oldptr,&oldpath);
    if(rc)return (uint64_t)(int64_t)rc;
    rc=nas_fs_user_path_begin(newdirfd,newptr,&newpath);
    if(rc) {
        nas_fs_path_end(&oldpath);
        return (uint64_t)(int64_t)rc;
    }
    struct nas_vfs_path source={0},new_parent={0};
    char new_leaf[256];
    rc=nas_fs_lookup_path_at(oldpath.start.dentry?&oldpath.start:NULL,
                             oldpath.root.dentry?&oldpath.root:NULL,
                             oldpath.pathname,(flags&AT_SYMLINK_FOLLOW)!=0,
                             &source);
    if(!rc)rc=nas_fs_lookup_parent_at_policy(
                       newpath.start.dentry?&newpath.start:NULL,
                       newpath.root.dentry?&newpath.root:NULL,
                       newpath.pathname,NAS_FS_TRAILING_CREATE_DEST,
                       &new_parent,new_leaf);
    if(!rc)rc=nas_vfs_link_path_child(&source,&new_parent,new_leaf);
    if(new_parent.dentry && nas_vfs_path_put(&new_parent))nas_fail("link parent ref");
    if(source.dentry && nas_vfs_path_put(&source))nas_fail("link source ref");
    nas_fs_path_end(&newpath);
    nas_fs_path_end(&oldpath);
    return (uint64_t)(int64_t)rc;
}

static uint64_t sys_getcwd_root(uint64_t buffer, uint64_t size) {
    if(TASK.fs->cwd_path.dentry) {
        char live[256];
        int err=nas_vfs_path_string(&TASK.fs->cwd_path,live);
        if(err)return (uint64_t)(int64_t)err;
        memcpy(current_workdir,live,strlen(live)+1);
    } else if(TASK.fs->cwd_pinned) {
        /* A pinned cwd MUST own a Dentry Path; no provider-supplied
         * inode->name escape hatch remains in the Linux ABI. */
        return (uint64_t)-ESTALE;
    }
    uint64_t length = 0;
    while (current_workdir[length]) length++;
    length++; /* Linux raw getcwd includes NUL in returned length. */
    if (size < length) return (uint64_t)-ERANGE;
    return nas_user_write_bytes(buffer, (const uint8_t *)current_workdir, length)
        ? length : (uint64_t)-EFAULT;
}
/* Transactional cwd ownership transfer. This consumes one independently
 * acquired Path reference; fchdir clones the existing File Dentry directly
 * while chdir first binds its resolved canonical inode. Neither path uses
 * provider directory_path/reverse-lookup nor a second inode resolution. */
static uint64_t set_cwd_owned(const struct nas_fs_node *node,
                              struct nas_vfs_path *next,
                              const char *canonical) {
    if(!next)return (uint64_t)-EINVAL;
    if(!node || !node->is_dir || !next->dentry) {
        if(next->dentry && nas_vfs_path_put(next))nas_fail("invalid cwd Path ref");
        return (uint64_t)-ENOTDIR;
    }
    /* The owned Dentry is authoritative; cwd[] is only the bounded legacy
     * display cache. A detached directory has no printable absolute name:
     * Linux fchdir(fd) succeeds but getcwd() reports ENOENT. NULL means
     * precisely this absence, never a cue to reopen an old cwd string. */
    if(canonical && strnlen(canonical,sizeof(TASK.fs->cwd))==sizeof(TASK.fs->cwd)) {
        if(nas_vfs_path_put(next))nas_fail("overlong cwd Path ref");
        return (uint64_t)-ENAMETOOLONG;
    }
    int rc=nas_vfs_node_pin(node);
    if(rc) {
        (void)nas_vfs_path_put(next);
        return (uint64_t)(int64_t)rc;
    }
    struct nas_fs_context *fs=TASK.fs;
    if(fs->cwd_path.dentry && nas_vfs_path_put(&fs->cwd_path))
        nas_fail("FS cwd path reference corrupted");
    if(fs->cwd_pinned && nas_vfs_node_unpin(&fs->cwd_node))
        nas_fail("FS cwd inode reference corrupted");
    if(canonical)memcpy(current_workdir,canonical,strlen(canonical)+1);
    else current_workdir[0]=0;
    fs->cwd_node=*node;
    fs->cwd_pinned=true;
    fs->cwd_path=*next;
    *next=(struct nas_vfs_path){0};
    return 0;
}
static uint64_t sys_chdir(uint64_t pathname) {
    struct nas_fs_path_request path;
    int rc=nas_fs_user_path_begin((uint64_t)-100,pathname,&path);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_vfs_path next={0};
    rc=nas_fs_lookup_path_at(path.start.dentry?&path.start:NULL,
                             path.root.dentry?&path.root:NULL,
                             path.pathname,true,&next);
    struct nas_fs_node node={0};char live[256];
    if(!rc)rc=nas_vfs_path_node(&next,&node);
    if(!rc) {
        int text_rc=nas_vfs_path_string(&next,live);
        if(text_rc && text_rc!=-ENOENT)rc=text_rc;
        else {
            uint64_t result=set_cwd_owned(&node,&next,
                                          text_rc ? NULL : live);
            if(next.dentry && nas_vfs_path_put(&next))nas_fail("chdir path ref");
            nas_fs_path_end(&path);
            return result;
        }
    }
    uint64_t result=(uint64_t)(int64_t)rc;
    if(next.dentry && nas_vfs_path_put(&next))nas_fail("chdir path ref");
    nas_fs_path_end(&path);
    return result;
}
static uint64_t sys_fchdir(uint64_t fd) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=
        &nas_open_files[TASK.fds->entries[fd].slot];
    if(!opened->vnode_pinned || !opened->vnode.is_dir)
        return (uint64_t)-ENOTDIR;
    /* fchdir must acquire the ALREADY OPEN File's Dentry, not rebuild its
     * Path by spelling and traversing a possibly renamed parent. This
     * succeeds without a second provider lookup and preserves identity. */
    if(!opened->vpath.dentry)return (uint64_t)-ESTALE;
    struct nas_vfs_path next={0};
    int rc=nas_vfs_path_get(&opened->vpath,&next);
    if(rc)return (uint64_t)(int64_t)rc;
    char live[256];
    rc=nas_vfs_path_string(&next,live);
    if(rc && rc!=-ENOENT) {
        (void)nas_vfs_path_put(&next);
        return (uint64_t)(int64_t)rc;
    }
    return set_cwd_owned(&opened->vnode,&next,rc ? NULL : live);
}


#define NAMESPACE_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_namespace_syscall(enum nas_syscall_op op,
                                            const uint64_t *args) {
    switch (op) {
    case NAS_SC_getcwd: NAMESPACE_RESULT(sys_getcwd_root(args[1], args[2]));
    case NAS_SC_chdir: NAMESPACE_RESULT(sys_chdir(args[1]));
    case NAS_SC_fchdir: NAMESPACE_RESULT(sys_fchdir(args[1]));
    case NAS_SC_link: NAMESPACE_RESULT(sys_linkat_tmp((uint64_t)-100,args[1],(uint64_t)-100,args[2],0));
    case NAS_SC_linkat: NAMESPACE_RESULT(sys_linkat_tmp(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_rename: NAMESPACE_RESULT(sys_renameat_tmp((uint64_t)-100, args[1], (uint64_t)-100, args[2], 0));
    case NAS_SC_renameat: NAMESPACE_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], 0));
    case NAS_SC_renameat2: NAMESPACE_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], args[5]));
    case NAS_SC_symlink: NAMESPACE_RESULT(sys_symlinkat_tmp(args[1], (uint64_t)-100, args[2]));
    case NAS_SC_symlinkat: NAMESPACE_RESULT(sys_symlinkat_tmp(args[1], args[2], args[3]));
    case NAS_SC_mkdir: NAMESPACE_RESULT(sys_mkdirat_tmp((uint64_t)-100, args[1], args[2]));
    case NAS_SC_mkdirat: NAMESPACE_RESULT(sys_mkdirat_tmp(args[1], args[2], args[3]));
    case NAS_SC_unlink: NAMESPACE_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0));
    case NAS_SC_rmdir: NAMESPACE_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0x200ULL));
    case NAS_SC_unlinkat: NAMESPACE_RESULT(sys_unlinkat_tmp(args[1], args[2], args[3]));
    default: return (struct nas_fs_result){ .handled=false, .value=0 };
    }
}
#undef NAMESPACE_RESULT
