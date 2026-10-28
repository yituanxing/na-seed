/* Pathname and namespace Linux ABI adapter. No filesystem-specific
 * mutation dispatch: VFS selects the real parent mount and its provider. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../path.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/mmu.h"
#include "mode.h"
#include "syscall.h"
#include "lookup.h"
#include "vfs.h"
#include "procfs.h"
#include "namespace_syscall.h"
#define TASK (*nas_current_task)
#define current_workdir TASK.fs->cwd

/* User path copy and dirfd ownership live here. The generic VFS resolves
 * dot/dotdot when they occur, before the legacy lexical path helper could
 * erase a non-directory intermediate component. */
/* Dot components are actual directory traversal, not mere string edits.
 * Ordinary paths retain the old normalized device-path API until devfs is
 * represented by VFS nodes. For dot components use VFS namei and canonical
 * parent inode, including for a final name that is not created yet. */
static bool has_dot_component(const char *src) {
    unsigned p=0;
    while(src[p]) {
        while(src[p]=='/')p++;
        unsigned start=p;
        while(src[p] && src[p]!='/')p++;
        unsigned len=p-start;
        if(len==1 && src[start]=='.')return true;
        if(len==2 && src[start]=='.' && src[start+1]=='.')return true;
    }
    return false;
}
static int resolve_component_path(const char *base,const char *src,
                                  char out[256]) {
    if(!has_dot_component(src))return nas_path_resolve(base,src,out);
    char raw[256],canon[256];
    int rc=nas_path_join_raw(base,src,raw);
    if(rc)return rc;
    struct nas_fs_node node;
    rc=nas_fs_lookup_node(raw,false,&node,canon);
    if(!rc) {
        memcpy(out,canon,strlen(canon)+1);
        return 0;
    }
    if(rc!=-ENOENT)return rc;
    /* Only a missing final basename may be created. Resolve the raw
     * parent (including symlinks and ..) before forming that basename.
     * A missing intermediate component must never be normalized away. */
    size_t n=strlen(raw);
    if(n<=1 || raw[n-1]=='/')return -ENOENT;
    size_t start=n;
    while(start>0 && raw[start-1]!='/')start--;
    if(!start)return -EINVAL;
    char parent_path[256];
    size_t prefix=start==1 ? 1 : start-1;
    memcpy(parent_path,raw,prefix);
    parent_path[prefix]=0;
    struct nas_fs_node parent;
    rc=nas_fs_lookup_node(parent_path,true,&parent,canon);
    if(rc)return rc;
    if(!parent.is_dir)return -ENOTDIR;
    size_t base_len=strlen(canon),leaf_len=n-start;
    if(base_len+(base_len>1?1U:0U)+leaf_len>=256)return -ENAMETOOLONG;
    memcpy(out,canon,base_len);
    if(base_len>1)out[base_len++]='/';
    memcpy(out+base_len,raw+start,leaf_len);
    out[base_len+leaf_len]=0;
    return 0;
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
    const char *base=0;
    char proc_base[256],inode_base[256];
    if (src[0]!='/') {
        if (dirfd==(uint64_t)-100) {
            if(TASK.fs->cwd_pinned) {
                int err=nas_vfs_directory_path(&TASK.fs->cwd_node,
                                                current_workdir,inode_base);
                if(err)return (uint64_t)(int64_t)err;
                base=inode_base;
            } else base=current_workdir;
        } else {
            if (dirfd>=NAS_FD_LIMIT || TASK.fds->entries[dirfd].kind==FD_CLOSED)
                return (uint64_t)-EBADF;
            if (TASK.fds->entries[dirfd].kind==FD_PROC_DIR) {
                unsigned slot=TASK.fds->entries[dirfd].slot;
                struct nas_proc_node node={nas_open_files[slot].proc_kind,
                                           nas_open_files[slot].proc_pid,
                                           nas_open_files[slot].proc_tgid,
                                           nas_open_files[slot].proc_fd};
                if(!nas_proc_dir_path(node,proc_base))return (uint64_t)-ENOENT;
                base=proc_base;
            } else {
                uint8_t kind=TASK.fds->entries[dirfd].kind;
                if(kind!=FD_DIR && kind!=FD_TMP_DIR)
                    return (uint64_t)-ENOTDIR;
                const struct nas_open_file *opened=
                    &nas_open_files[TASK.fds->entries[dirfd].slot];
                if(!opened->vnode_pinned || !opened->vnode.is_dir)
                    return (uint64_t)-ENOTDIR;
                int err=nas_vfs_directory_path(&opened->vnode,
                                                opened->origin_path,inode_base);
                if(err)return (uint64_t)(int64_t)err;
                base=inode_base;
            }
        }
    }
    return (uint64_t)(int64_t)resolve_component_path(base,src,out);
}

/* Linux syscall decoders retain user-pointer validation and task-relative
 * path resolution; the RAM filesystem backend owns namespace operations. */
static uint64_t sys_mkdirat_tmp(uint64_t dirfd, uint64_t pathname,
                                uint64_t mode) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)nas_vfs_mkdir(path,
                            nas_fs_create_mode(mode,TASK.fs->umask));
}
static uint64_t sys_unlinkat_tmp(uint64_t dirfd, uint64_t pathname,
                                 uint64_t flags) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)nas_vfs_unlink(path, flags);
}
static uint64_t sys_renameat_tmp(uint64_t olddirfd, uint64_t oldptr,
                                 uint64_t newdirfd, uint64_t newptr,
                                 uint64_t flags) {
    /* Reject unsupported flags before reading user paths, as before. */
    if (flags & ~1ULL) return (uint64_t)-EINVAL;
    char oldpath[256], newpath[256];
    uint64_t ret = nas_fs_resolve_path(olddirfd, oldptr, oldpath);
    if ((int64_t)ret < 0) return ret;
    ret = nas_fs_resolve_path(newdirfd, newptr, newpath);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)nas_vfs_rename(oldpath, newpath, flags);
}
static uint64_t sys_symlinkat_tmp(uint64_t targetptr,uint64_t dirfd,
                                  uint64_t linkptr) {
    char target[256], linkpath[256];
    if (targetptr>=USER_LIMIT) return (uint64_t)-EFAULT;
    unsigned i=0;
    for (;i<sizeof(target);i++) {
        uint8_t *p=nas_mmu_user_byte(targetptr+i);
        if (!p) return (uint64_t)-EFAULT;
        target[i]=(char)*p;
        if (!target[i]) break;
    }
    if (i==sizeof(target)) return (uint64_t)-ENAMETOOLONG;
    uint64_t ret=nas_fs_resolve_path(dirfd,linkptr,linkpath);
    return (int64_t)ret<0 ? ret :
        (uint64_t)(int64_t)nas_vfs_symlink(target,linkpath);
}

/* Hardlinks reuse the same tmpfs inode: no copies, no fake CPIO writes.
 * AT_SYMLINK_FOLLOW applies only to the old endpoint of linkat(). */
static uint64_t sys_linkat_tmp(uint64_t olddirfd,uint64_t oldptr,
                               uint64_t newdirfd,uint64_t newptr,
                               uint64_t flags) {
    const uint64_t AT_SYMLINK_FOLLOW=0x400ULL;
    if(flags & ~AT_SYMLINK_FOLLOW)return (uint64_t)-EINVAL;
    char oldpath[256],newpath[256];
    uint64_t ret=nas_fs_resolve_path(olddirfd,oldptr,oldpath);
    if((int64_t)ret<0)return ret;
    if(flags & AT_SYMLINK_FOLLOW) {
        char actual[256];
        int err=nas_fs_resolve_existing(oldpath,true,actual);
        if(err)return (uint64_t)(int64_t)err;
        unsigned i=0;
        do{oldpath[i]=actual[i];}while(actual[i++]);
    }
    ret=nas_fs_resolve_path(newdirfd,newptr,newpath);
    return (int64_t)ret<0 ? ret :
        (uint64_t)(int64_t)nas_vfs_link(oldpath,newpath);
}

static uint64_t sys_getcwd_root(uint64_t buffer, uint64_t size) {
    if(TASK.fs->cwd_pinned) {
        char live[256];
        int err=nas_vfs_directory_path(&TASK.fs->cwd_node,current_workdir,live);
        if(err)return (uint64_t)(int64_t)err;
        memcpy(current_workdir,live,strlen(live)+1);
    }
    uint64_t length = 0;
    while (current_workdir[length]) length++;
    length++; /* Linux raw getcwd includes NUL in returned length. */
    if (size < length) return (uint64_t)-ERANGE;
    return nas_user_write_bytes(buffer, (const uint8_t *)current_workdir, length)
        ? length : (uint64_t)-EFAULT;
}
/* FS-context-owned cwd ref: no extra ISA/provider-specific unmount checks. */
static uint64_t set_cwd_from_path(const char *path) {
    char resolved[256];
    struct nas_fs_node node;
    int rc=nas_fs_lookup_node(path,true,&node,resolved);
    if(rc)return (uint64_t)(int64_t)rc;
    if(!node.is_dir)return (uint64_t)-ENOTDIR;
    rc=nas_vfs_node_pin(&node);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_fs_context *fs=TASK.fs;
    if(fs->cwd_pinned && nas_vfs_node_unpin(&fs->cwd_node)) {
        (void)nas_vfs_node_unpin(&node);
        return (uint64_t)-EIO;
    }
    memcpy(current_workdir,resolved,strlen(resolved)+1);
    fs->cwd_node=node;
    fs->cwd_pinned=true;
    return 0;
}
static uint64_t sys_chdir(uint64_t pathname) {
    char path[256];
    uint64_t rc=nas_fs_resolve_path((uint64_t)-100,pathname,path);
    return (int64_t)rc<0 ? rc : set_cwd_from_path(path);
}
static uint64_t sys_fchdir(uint64_t fd) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED)
        return (uint64_t)-EBADF;
    const char *path;
    char proc_base[256];
    if (TASK.fds->entries[fd].kind == FD_PROC_DIR) {
        unsigned slot=TASK.fds->entries[fd].slot;
        struct nas_proc_node node={nas_open_files[slot].proc_kind,
                                   nas_open_files[slot].proc_pid,
                                           nas_open_files[slot].proc_tgid,
                                           nas_open_files[slot].proc_fd};
        if(!nas_proc_dir_path(node,proc_base))return (uint64_t)-ENOENT;
        path=proc_base;
    } else {
        uint8_t kind=TASK.fds->entries[fd].kind;
        if(kind!=FD_DIR && kind!=FD_TMP_DIR)return (uint64_t)-ENOTDIR;
        const struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[fd].slot];
        if(!opened->vnode_pinned || !opened->vnode.is_dir)
            return (uint64_t)-ENOTDIR;
        int err=nas_vfs_directory_path(&opened->vnode,
                                        opened->origin_path,proc_base);
        if(err)return (uint64_t)(int64_t)err;
        path=proc_base;
    }
    return set_cwd_from_path(path);
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
