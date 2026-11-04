/* Pathname and namespace Linux ABI adapter. No filesystem-specific
 * mutation dispatch: VFS selects the real parent mount and its provider. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../panic.h"
#include "../task.h"
#include "../fd.h"
#include "../tmpfs.h"
#include "../arch/linux_fcntl_abi.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/mmu.h"
#include "../mm/heap.h"
#include "../mm/frame.h"
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
    if(request->pathname && request->pathname!=request->inline_path &&
       !nas_heap_free(request->pathname))
        nas_fail("path request heap pathname ownership corrupted");
    request->pathname=0;
}

/* One owner for the cwd/dirfd/root Path lifetime, shared by user and kernel
 * pathname entry points. The caller has already copied a complete pathname
 * into either inline or owned heap memory. */
static int begin_anchored_path(uint64_t dirfd,
                               struct nas_fs_path_request *request) {
    int rc=nas_vfs_path_root(&request->root);
    /* Original direct-ELF boots can have mounted children but no root
     * archive. Path namei has a rootless fallback; keep a NULL clamp only
     * for this precise case, never for a failed mounted lookup. */
    if(rc==-ENOENT)rc=0;
    else if(rc) {nas_fs_path_end(request);return rc;}
    if(request->pathname[0]=='/')return 0;
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

int nas_fs_kernel_path_begin(uint64_t dirfd,const char *kernel_path,
                             struct nas_fs_path_request *request) {
    if(!kernel_path || !request)return -EINVAL;
    *request=(struct nas_fs_path_request){0};
    size_t length=strnlen(kernel_path,NAS_VFS_WALK_PATH_LIMIT);
    if(length==NAS_VFS_WALK_PATH_LIMIT)return -ENAMETOOLONG;
    if(!length)return -ENOENT;
    request->pathname=request->inline_path;
    if(length>=sizeof(request->inline_path)) {
        request->pathname=nas_heap_alloc(length+1);
        if(!request->pathname)return -ENOMEM;
    }
    memcpy(request->pathname,kernel_path,length+1);
    return begin_anchored_path(dirfd,request);
}

int nas_fs_user_path_begin(uint64_t dirfd,uint64_t user_pointer,
                           struct nas_fs_path_request *request) {
    if(!request)return -EINVAL;
    *request=(struct nas_fs_path_request){0};
    request->pathname=request->inline_path;
    int rc=nas_user_read_cstr(user_pointer,request->inline_path,
                              sizeof(request->inline_path));
    if(rc==-ENAMETOOLONG) {
        /* Namei already accepts PATH_MAX strings. Keep only the rare long
         * syscall pathname in the heap instead of inflating every stack
         * frame using nas_fs_path_request. */
        request->pathname=nas_heap_alloc(NAS_VFS_WALK_PATH_LIMIT);
        if(!request->pathname)return -ENOMEM;
        rc=nas_user_read_cstr(user_pointer,request->pathname,
                              NAS_VFS_WALK_PATH_LIMIT);
    }
    if(!rc && !request->pathname[0])rc=-ENOENT;
    if(rc) {nas_fs_path_end(request);return rc;}
    return begin_anchored_path(dirfd,request);
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

/* Linux syscall decoders retain user-pointer validation and task-relative
 * path resolution; the RAM filesystem backend owns namespace operations. */
static uint64_t sys_mkdirat_tmp(uint64_t dirfd, uint64_t pathname,
                                uint64_t mode) {
    struct nas_fs_path_request path;
    int rc=nas_fs_user_path_begin(dirfd,pathname,&path);
    if(rc)return (uint64_t)(int64_t)rc;
    /* The Linux VFS reports an existing root as EEXIST for mkdir("/").
     * A parent/name lookup has no final leaf for a slash-only spelling. */
    if(nas_fs_root_only(path.pathname)) {
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
    if(flags&~0x200ULL)return (uint64_t)-EINVAL;
    struct nas_fs_path_request path;
    int rc=nas_fs_user_path_begin(dirfd,pathname,&path);
    if(rc)return (uint64_t)(int64_t)rc;
    if(nas_fs_root_only(path.pathname)) {
        nas_fs_path_end(&path);
        return (uint64_t)(flags&0x200ULL ? -EBUSY : -EISDIR);
    }
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
    /* The VFS and tmpfs both implement NOREPLACE and EXCHANGE. Keep the
     * syscall entrypoint in sync: rejecting bit 1 here made genuine Linux
     * renameat2(RENAME_EXCHANGE) unreachable despite backend host tests. */
    if ((flags & ~3ULL) || flags == 3ULL) return (uint64_t)-EINVAL;
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
    int read_rc=nas_user_read_cstr(targetptr,target,sizeof(target));
    if(read_rc)return (uint64_t)(int64_t)read_rc;
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
        char *path=live;
        if(err==-ENAMETOOLONG) {
            /* Deep cwd identity is the owned Dentry, not cwd[256]. Only
             * exceptional long getcwd requests need a PATH_MAX heap copy. */
            path=nas_heap_alloc(NAS_VFS_WALK_PATH_LIMIT);
            if(!path)return (uint64_t)-ENOMEM;
            err=nas_vfs_path_string_buf(&TASK.fs->cwd_path,path,
                                        NAS_VFS_WALK_PATH_LIMIT);
        }
        if(err) {
            if(path!=live)(void)nas_heap_free(path);
            return (uint64_t)(int64_t)err;
        }
        uint64_t length=strlen(path)+1U;
        if(length<=sizeof(TASK.fs->cwd))
            memcpy(current_workdir,path,(size_t)length);
        else current_workdir[0]=0; /* optional short diagnostic only */
        uint64_t result=size<length ? (uint64_t)-ERANGE :
            (nas_user_write_bytes(buffer,(const uint8_t *)path,length) ?
                length : (uint64_t)-EFAULT);
        if(path!=live)(void)nas_heap_free(path);
        return result;
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
    /* Long absolute paths have no 256-byte display cache, but remain
     * perfectly valid owned cwd Paths. Never fail chdir/fchdir because a
     * diagnostic cannot represent the already-resolved directory. */
    if(canonical && strnlen(canonical,sizeof(TASK.fs->cwd))==sizeof(TASK.fs->cwd))
        canonical=NULL;
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
        if(text_rc && text_rc!=-ENOENT && text_rc!=-ENAMETOOLONG)rc=text_rc;
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
    if(rc && rc!=-ENOENT && rc!=-ENAMETOOLONG) {
        (void)nas_vfs_path_put(&next);
        return (uint64_t)(int64_t)rc;
    }
    return set_cwd_owned(&opened->vnode,&next,rc ? NULL : live);
}

/* Linux mount(2) is a namespace operation, not an architecture-specific
 * backdoor into the boot filesystem. Accept only real supported providers
 * and options; the VFS already owns the mount graph and covered dentries. */
static int mount_target(uint64_t pointer,char target[256]) {
    struct nas_fs_path_request request;
    struct nas_vfs_path found={0};
    struct nas_fs_node node={0};
    int rc=nas_fs_user_lookup_begin((uint64_t)-100,pointer,true,
                                     &request,&found,&node);
    if(rc)return rc;
    if(!node.is_dir)rc=-ENOTDIR;
    if(!rc)rc=nas_vfs_path_string_buf(&found,target,256);
    if(found.dentry && nas_vfs_path_put(&found))nas_fail("mount target ref");
    nas_fs_path_end(&request);
    return rc;
}
/* Values are enforced by the tmpfs superblock's actual data-page and inode
 * accounting, not discarded because OpenRC happens to request large limits.
 * tmpfs sizes use percentages of managed RAM or binary K/M/G byte units. */
static int parse_scaled_number(const char *text,uint64_t *value) {
    if(!text || *text<'0' || *text>'9')return -EINVAL;
    uint64_t number=0;
    while(*text>='0' && *text<='9') {
        unsigned digit=(unsigned)(*text++-'0');
        if(number>(UINT64_MAX-digit)/10ULL)return -ERANGE;
        number=number*10ULL+digit;
    }
    unsigned shift=0;
    if(*text=='k' || *text=='K')shift=10;
    else if(*text=='m' || *text=='M')shift=20;
    else if(*text=='g' || *text=='G')shift=30;
    if(shift)text++;
    if(*text || number>(UINT64_MAX>>shift))return -ERANGE;
    *value=number<<shift;
    return number ? 0 : -EINVAL;
}
static int parse_tmpfs_options(const char *data,uint32_t *mode,
                                uint64_t *max_pages,uint64_t *max_inodes) {
    *mode=0755U;
    uint64_t frames=nas_managed_frames();
    if(!frames)return -ENOMEM;
    *max_pages=frames/2ULL ? frames/2ULL : 1;
    *max_inodes=frames/2ULL ? frames/2ULL : 1;
    if(!data[0])return 0;
    const char *cursor=data;
    while(*cursor) {
        char option[64];unsigned n=0;
        while(*cursor && *cursor!=',') {
            if(n+1>=sizeof(option))return -EINVAL;
            option[n++]=*cursor++;
        }
        option[n]=0;
        if(*cursor==',')cursor++;
        if(!strncmp(option,"mode=",5)) {
            if(!option[5])return -EINVAL;
            uint32_t value=0;
            for(const char *digit=option+5;*digit;digit++) {
                if(*digit<'0' || *digit>'7' || value>0777U)
                    return -EINVAL;
                value=(value<<3)+(uint32_t)(*digit-'0');
            }
            if(value>07777U)return -EINVAL;
            *mode=value;
        } else if(!strncmp(option,"size=",5)) {
            size_t length=strlen(option+5);
            if(length && option[5+length-1]=='%') {
                option[5+length-1]=0;
                uint64_t percent;
                int rc=parse_scaled_number(option+5,&percent);
                if(rc)return rc;
                if(percent>100)return -EINVAL;
                /* Avoid overflow even with a large physical frame count. */
                *max_pages=(frames/100ULL)*percent+
                           ((frames%100ULL)*percent)/100ULL;
                if(!*max_pages)*max_pages=1;
            } else {
                uint64_t bytes;
                int rc=parse_scaled_number(option+5,&bytes);
                if(rc)return rc;
                *max_pages=bytes/4096ULL+((bytes%4096ULL)!=0);
            }
        } else if(!strncmp(option,"nr_inodes=",10)) {
            int rc=parse_scaled_number(option+10,max_inodes);
            if(rc)return rc;
        } else if(strcmp(option,"nosuid") && strcmp(option,"nodev") &&
                  strcmp(option,"rw") && strcmp(option,"ro"))
            return -EOPNOTSUPP;
    }
    return 0;
}
static uint64_t sys_mount(uint64_t source_ptr,uint64_t target_ptr,
                          uint64_t type_ptr,uint64_t flags,
                          uint64_t data_ptr) {
    /* MS_RDONLY=1, MS_NOSUID=2, MS_NODEV=4, MS_NOEXEC=8 and
     * MS_STRICTATIME=1<<24. Procfs has no executable regular files,
     * device nodes or set-ID transitions; runtime tmpfs implements
     * strict atime and separately limits its data pages and inodes.
     * Bind/remount/propagation and tmpfs noexec remain unsupported. */
    char source[64]={0},fstype[32],target[256],data[256]={0};
    int rc=0;
    if(source_ptr)rc=nas_user_read_cstr(source_ptr,source,sizeof(source));
    if(!rc && !type_ptr)rc=-EINVAL;
    if(!rc)rc=nas_user_read_cstr(type_ptr,fstype,sizeof(fstype));
    if(!rc && data_ptr)rc=nas_user_read_cstr(data_ptr,data,sizeof(data));
    if(rc)return (uint64_t)(int64_t)rc;
#if defined(NAS_ENABLE_M191_ALPINE_ROOT)
    /* Bounded original Alpine mount(2) evidence, to distinguish actual
     * BusyBox VFS flags/data from OpenRC's shell-level option string. */
    nas_print("[M192 MOUNT ABI] source=");nas_print(source);
    nas_print(" fstype=");nas_print(fstype);
    nas_print(" flags=");nas_hex(flags);
    nas_print(" data=");nas_print(data);nas_print("\n");
#endif
    if(strcmp(fstype,"tmpfs") && strcmp(fstype,"proc"))
        return (uint64_t)-ENODEV;
    rc=mount_target(target_ptr,target);
    if(rc)return (uint64_t)(int64_t)rc;
    if(!strcmp(fstype,"proc")) {
        if(strcmp(source,"proc") || (flags&~14ULL) || data[0] ||
           strcmp(target,"/proc"))return (uint64_t)-EINVAL;
        /* /proc is already a permanent, genuine procfs attachment.
         * Idempotence is limited to the exact same existing superblock. */
        for(unsigned i=0;;i++) {
            struct nas_vfs_mount_snapshot entry;
            rc=nas_vfs_mount_snapshot(i,&entry);
            if(rc)return (uint64_t)-ENODEV;
            if(!strcmp(entry.target,"/proc"))
                return !strcmp(entry.fstype,"procfs") &&
                       !strcmp(entry.source,"procfs") ?
                       0 : (uint64_t)-EBUSY;
        }
    }
    if(flags&~(7ULL|(1ULL<<24)))return (uint64_t)-EOPNOTSUPP;
    if(!source[0])return (uint64_t)-EINVAL;
    uint32_t mode;
    uint64_t max_pages,max_inodes;
    rc=parse_tmpfs_options(data,&mode,&max_pages,&max_inodes);
    if(!rc)rc=nas_vfs_mount_tmpfs(target,source,mode,(flags&1ULL)!=0,
                                  max_pages,max_inodes,(flags&(1ULL<<24))!=0);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_umount2(uint64_t target_ptr,uint64_t flags) {
    if(flags)return (uint64_t)-EOPNOTSUPP;
    char target[256];
    int rc=mount_target(target_ptr,target);
    if(!rc)rc=nas_vfs_umount_tmpfs(target);
    return (uint64_t)(int64_t)rc;
}


#define NAMESPACE_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_namespace_syscall(enum nas_syscall_op op,
                                            const uint64_t *args) {
    switch (op) {
    case NAS_SC_getcwd: NAMESPACE_RESULT(sys_getcwd_root(args[1], args[2]));
    case NAS_SC_chdir: NAMESPACE_RESULT(sys_chdir(args[1]));
    case NAS_SC_fchdir: NAMESPACE_RESULT(sys_fchdir(args[1]));
    case NAS_SC_mount: NAMESPACE_RESULT(sys_mount(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_umount2: NAMESPACE_RESULT(sys_umount2(args[1],args[2]));
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
