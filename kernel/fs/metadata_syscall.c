/* Shared CPIO/tmpfs stat, directory and mutable inode Linux ABI. */
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../drivers/display/fbdev.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../arch/linux_fcntl_abi.h"
#include "io.h"
#include "../arch/linux_stat_abi.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "../time/clock.h"
#include "lookup.h"
#include "dac.h"
#include "vfs.h"
#include "procfs.h"
#include "syscall.h"
#include "open_path.h"
#include "metadata_syscall.h"
#include "path_request.h"
#include "../panic.h"
#define TASK (*nas_current_task)

static void end_mounted_lookup(struct nas_fs_path_request *request,
                                struct nas_vfs_path *path) {
    if(path->dentry && nas_vfs_path_put(path))
        nas_fail("metadata lookup Path reference corrupted");
    nas_fs_path_end(request);
}
/* Linux-visible bootstrap metadata is a read-only namespace projection.
 * It must not acquire an OpenFile, allocate a PTY or depend on spare FDs.
 * Both stat(path) and fstat(open(path)) use the SAME device identity. */
static void fill_bootstrap_stat(const struct nas_fs_bootstrap_info *info,
                                struct nas_linux_stat *st) {
    *st=(struct nas_linux_stat){0};
    st->dev=info->dev;st->ino=info->ino;st->rdev=info->rdev;
    st->mode=(uint32_t)info->mode;st->nlink=1;st->blksize=PAGE;
    st->size=(int64_t)info->size;
    st->blocks=(int64_t)((info->size+511U)/512U);
}
static uint64_t write_bootstrap_stat(const struct nas_fs_bootstrap_info *info,
                                     uint64_t address) {
    struct nas_linux_stat st;
    fill_bootstrap_stat(info,&st);
    return nas_user_write_bytes(address,(const uint8_t *)&st,sizeof(st)) ?
           0:(uint64_t)-EFAULT;
}

/* Generic inode stat comes from the filesystem lookup result, never a
 * fixed ext4-only number or an exported kernel pointer. */
static int fill_mounted_stat(const struct nas_fs_node *node,
                             struct nas_linux_stat *st) {
    if(!node || !node->filesystem || !node->ino)return -EINVAL;
    struct nas_fs_node fresh;
    int rc=nas_vfs_getattr(node,&fresh);
    if(rc)return rc;
    *st=(struct nas_linux_stat){0};
    st->dev=nas_vfs_node_device(&fresh);
    if(!st->dev)return -ENODEV;
    st->ino=fresh.ino;st->mode=fresh.mode;
    st->rdev=fresh.rdev;
    st->nlink=fresh.nlink; /* An unlinked but open inode has zero links. */
    st->uid=fresh.uid;st->gid=fresh.gid;
    uint64_t live_size=fresh.size;
    st->size=(int64_t)live_size;
    st->blocks=(int64_t)((live_size+511)/512);
    st->blksize=PAGE;
    st->atime_sec=(int64_t)fresh.atime_sec;
    st->mtime_sec=(int64_t)fresh.mtime_sec;
    st->ctime_sec=(int64_t)fresh.ctime_sec;
    st->atime_nsec=fresh.atime_nsec;
    st->mtime_nsec=fresh.mtime_nsec;
    st->ctime_nsec=fresh.ctime_nsec;
    return 0;
}
static uint64_t write_mounted_stat(const struct nas_fs_node *node,
                                   uint64_t destination) {
    struct nas_linux_stat st;
    int rc=fill_mounted_stat(node,&st);
    if(rc)return (uint64_t)(int64_t)rc;
    return nas_user_write_bytes(destination,(const uint8_t *)&st,sizeof(st))
        ? 0:(uint64_t)-EFAULT;
}
/* Linux statfs and fstatfs query the same already-resolved mount/inode.
 * No provider sees a user pointer, pathname or Linux-native wire layout. */
static uint64_t write_mounted_statfs(const struct nas_fs_node *node,
                                     uint64_t destination) {
    struct nas_vfs_statfs fs;
    int rc=nas_vfs_statfs(node,&fs);
    if(rc)return (uint64_t)(int64_t)rc;
    uint64_t device=nas_vfs_node_device(node);
    if(!device)return (uint64_t)-ENODEV;
    struct nas_linux_statfs result={0};
    result.type=(int64_t)fs.magic;
    result.bsize=(int64_t)fs.block_size;
    result.blocks=fs.blocks;
    result.bfree=fs.free_blocks;
    result.bavail=fs.available_blocks;
    result.files=fs.files;
    result.ffree=fs.free_files;
    result.fsid[0]=(int32_t)(uint32_t)device;
    result.fsid[1]=(int32_t)(uint32_t)(device>>32);
    result.namelen=(int64_t)fs.name_max;
    result.frsize=(int64_t)fs.fragment_size;
    result.flags=(node->mount->read_only || node->filesystem->read_only)
        ? 1 : 0; /* Linux ST_RDONLY */
    return nas_user_write_bytes(destination,(const uint8_t *)&result,
                                sizeof(result)) ? 0:(uint64_t)-EFAULT;
}
static uint64_t sys_fstatfs_boot(uint64_t fd,uint64_t address) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=
        &nas_open_files[TASK.fds->entries[fd].slot];
    if(!opened->vnode_pinned)return (uint64_t)-EOPNOTSUPP;
    return write_mounted_statfs(&opened->vnode,address);
}
static uint64_t sys_statfs_boot(uint64_t pathname,uint64_t address) {
    struct nas_fs_path_request request;
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    int rc=nas_fs_user_lookup_begin((uint64_t)-100,pathname,true,
                                    &request,&path,&node);
    if(rc)return (uint64_t)(int64_t)rc;
    uint64_t value=write_mounted_statfs(&node,address);
    end_mounted_lookup(&request,&path);
    return value;
}
/* Linux x86-64 struct stat layout, 144 bytes. File size is actual module.
 * Device entries have character-device type; mounted files are regular.
 */
static uint64_t sys_fstat_boot(uint64_t fd, uint64_t address) {
    if (fd >= NAS_FD_LIMIT || !TASK.fds->entries[fd].kind)
        return (uint64_t)-EBADF;
    /* Any opened VFS inode, regardless of bootfs/tmpfs/procfs/ext4
     * branding, has exactly one owner and one stat encoder. fstat(fd)
     * and fstatat(path) report the same mount identity and timestamps. */
    const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->vnode_pinned)
        return write_mounted_stat(&opened->vnode,address);
    if(opened->boot_identity.valid) {
        const struct nas_fs_bootstrap_info identity={
            .dev=opened->boot_identity.dev,
            .ino=opened->boot_identity.ino,
            .rdev=opened->boot_identity.rdev,
            .mode=opened->boot_identity.mode,
            .size=opened->boot_identity.size
        };
        return write_bootstrap_stat(&identity,address);
    }
    const uint8_t kind=TASK.fds->entries[fd].kind;
    struct nas_linux_stat st={0};
    st.nlink=1;st.blksize=PAGE;
    if (kind == FD_PIPE_R || kind == FD_PIPE_W || kind == FD_SOCKET) {
        st.dev = 4; st.ino = (uint64_t)TASK.fds->entries[fd].slot + 1000;
        st.mode = kind == FD_SOCKET ? 0140777 : 0010600;
        /* Linux presents socketpair as S_IFSOCK, pipe as S_IFIFO. */
    } else if(kind==FD_BOOTFS) {
        const struct limine_file *file=opened->boot_file;
        if (!file) return (uint64_t)-EBADF;
        const struct nas_fs_bootstrap_info module={
            .dev=1,.ino=nas_fs_bootstrap_module_ino(file),
            .mode=0100555U,.size=file->size
        };
        return write_bootstrap_stat(&module,address);
    } else if(kind==FD_FRAMEBUFFER) {
        /* Relative openat("fb0") under /dev has the same device identity:
         * fstat must not depend on the optional origin-path spelling. */
        struct nas_fs_bootstrap_info info;
        if(nas_fs_bootstrap_describe("/dev/fb0",&info))
            return (uint64_t)-ENODEV;
        return write_bootstrap_stat(&info,address);
    } else if(kind==FD_PTY_MASTER || kind==FD_PTY_SLAVE) {
        unsigned number=nas_pty_number(TASK.fds->entries[fd].slot);
        const struct nas_fs_bootstrap_info pty={
            .dev=6,.ino=kind==FD_PTY_MASTER ? 2000U :
                        2000U+(uint64_t)number,
            .rdev=kind==FD_PTY_MASTER ? ((5U<<8)|2U) :
                  ((136U<<8)|(uint64_t)number),
            .mode=0020620U
        };
        return write_bootstrap_stat(&pty,address);
    } else {
        const char *path=opened->origin_path;
        if(!path[0]) {
            path=kind==FD_NULL ? "/dev/null" :
                 kind==FD_ZERO ? "/dev/zero" :
                 kind==FD_RANDOM ? "/dev/urandom" : "/dev/console";
        }
        struct nas_fs_bootstrap_info info;
        if(!nas_fs_bootstrap_describe(path,&info) &&
           info.fd_kind!=FD_BOOTFS)
            return write_bootstrap_stat(&info,address);
        /* The original bootstrap UART is not a named VFS inode. Keep its
         * console identity stable across dup and across separate FDs. */
        const struct nas_fs_bootstrap_info console={
            .dev=2,.ino=101,.rdev=(5U<<8)|1U,.mode=0020666U
        };
        return write_bootstrap_stat(&console,address);
    }
    if (!nas_user_write_bytes(address, (const uint8_t *)&st, sizeof(st)))
        return (uint64_t)-EFAULT;
    return 0;
}
/* In a volatile tmpfs there is no asynchronous durable writeback.
 * fsync/fdatasync return success only for actual open RAM inodes. */
static uint64_t sys_sync_fd(uint64_t fd) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(!opened->vnode_pinned)return (uint64_t)-EINVAL;
    return (uint64_t)(int64_t)nas_vfs_sync_user(&opened->vnode);
}
static uint64_t sys_resize_fd(uint64_t fd,uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    const unsigned slot=TASK.fds->entries[fd].slot;
    struct nas_open_file *opened=&nas_open_files[slot];
    if(TASK.fds->entries[fd].kind==FD_BOOTFS)return (uint64_t)-EROFS;
    /* File owns an inode, not a filesystem-specific FD label. A provider
     * that supports resize participates without a syscall code change.
     * Socket/pipe/device descriptions still have no VFS vnode. */
    if(!opened->vnode_pinned)return (uint64_t)-EINVAL;
    if(opened->vnode.is_dir)return (uint64_t)-EISDIR;
    if((opened->status_flags&3ULL)==0)return (uint64_t)-EINVAL;
    int rc=nas_vfs_resize(&opened->vnode,length);
    if(!rc)opened->vnode.size=length;
    return (uint64_t)(int64_t)rc;
}
/* Linux fadvise64 provides advisory cache hints, not a data mutation. It is
 * legal to ignore every valid hint while still checking the actual inode FD,
 * ranges and advice code; unsupported pipe/device handles remain errors. */
static uint64_t sys_fadvise_fd(uint64_t fd,uint64_t offset,
                              uint64_t length,uint64_t advice) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=
        &nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if(advice>5U || offset>INT64_MAX || length>INT64_MAX ||
       (length && offset>INT64_MAX-length))return (uint64_t)-EINVAL;
    if(!opened->vnode_pinned)return (uint64_t)-ESPIPE;
    if(opened->vnode.is_dir)return (uint64_t)-EINVAL;
    return 0;
}
/* Unlike ftruncate this must reserve backing store. The generic VFS guards
 * the inode; a provider without genuine allocation support reports EOPNOTSUPP
 * rather than pretending a sparse file is preallocated. */
static uint64_t sys_preallocate_fd(uint64_t fd,uint64_t mode,
                                  uint64_t offset,uint64_t length) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if((opened->status_flags&3ULL)==0)return (uint64_t)-EBADF;
    if(mode&~1ULL)return (uint64_t)-EOPNOTSUPP;
    if(!length)return (uint64_t)-EINVAL;
    if(offset>INT64_MAX || length>INT64_MAX-offset)return (uint64_t)-EFBIG;
    if(!opened->vnode_pinned)return (uint64_t)-ESPIPE;
    if(opened->vnode.is_dir)return (uint64_t)-EISDIR;
    int rc=nas_vfs_preallocate(&opened->vnode,offset,length,(unsigned)mode);
    if(!rc && !(mode&1ULL))opened->vnode.size=nas_vfs_node_size(&opened->vnode);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_resize_path(uint64_t pathname,uint64_t length) {
    struct nas_fs_path_request request;
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    int rc=nas_fs_user_lookup_begin((uint64_t)-100,pathname,true,
                                    &request,&path,&node);
    if(rc)return (uint64_t)(int64_t)rc;
    rc=nas_dac_mode(&node,NAS_DAC_WRITE);
    if(!rc)rc=nas_vfs_resize(&node,length);
    end_mounted_lookup(&request,&path);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_lseek_boot(uint64_t fd, uint64_t off, uint64_t whence) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED)
        return (uint64_t)-EBADF;
    if(nas_open_files[TASK.fds->entries[fd].slot].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    uint64_t size=0;
    bool directory=false;
    /* A mounted fbdev inode supplies path/stat ownership, but the device
     * still owns the seekable scanout length. Mounting /dev must preserve
     * the same seek semantics as the rootless chrdev bootstrap. */
    if(TASK.fds->entries[fd].kind==FD_FRAMEBUFFER && nas_fbdev_available())
        size=nas_fbdev_length();
    else if(opened->vnode_pinned) {
        /* The same open inode lease supplies seek/read/write and fstat.
         * No tmpfs slot inspection, per-mount filename or FD_* enumeration. */
        if(!opened->vnode.filesystem ||
           (!opened->vnode.is_dir && !opened->vnode.filesystem->read_at))
            return (uint64_t)-ESPIPE;
        directory=opened->vnode.is_dir;
        size=nas_vfs_node_size(&opened->vnode);
    } else if(TASK.fds->entries[fd].kind==FD_BOOTFS &&
             opened->boot_file)
        size=opened->boot_file->size; /* only unmounted Limine modules */
    else return (uint64_t)-ESPIPE;
    uint64_t base=0;
    if(whence==1)base=opened->position;
    else if(whence==2 && !directory)base=size;
    else if(whence!=0)return (uint64_t)-EINVAL;
    int64_t signed_off = (int64_t)off;
    if (signed_off < 0) {
        uint64_t magnitude = 0 - off;
        if (magnitude > base) return (uint64_t)-EINVAL;
        base -= magnitude;
    } else {
        if (base > UINT64_MAX - off) return (uint64_t)-EINVAL;
        base += off;
    }
    opened->position=base;
    return base;
}
/* Linux access/faccessat/faccessat2: existence and permissions are
 * checked against the actual CPIO /tmp/device namespace, not a fabricated
 * "always exists" or "always readable" response.  Our current credentials
 * are root: read/write bypass DAC bits, but execute needs at least one X bit.
 * An immutable CPIO file cannot be written even by root (EROFS).
 */
static uint64_t sys_access_path(uint64_t dirfd,uint64_t pathname,
                                uint64_t mode,uint64_t flags) {
    const uint64_t AT_EACCESS=0x200ULL;
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    if((mode & ~7ULL) || (flags & ~(AT_EACCESS|AT_SYMLINK_NOFOLLOW)))
        return (uint64_t)-EINVAL;
    uint32_t bits=0;
    bool writable=false;
    bool is_dir=false;
    struct nas_fs_path_request request;
    struct nas_vfs_path located={0};
    struct nas_fs_node node={0};
    /* Keep the original copied pathname AND its owned root/dirfd anchors
     * alive through both mounted lookup and the narrowly permitted
     * bootstrap fallback. No second usercopy or lexical re-resolution. */
    int err=nas_fs_user_path_begin(dirfd,pathname,&request);
    if(err)return (uint64_t)(int64_t)err;
    err=nas_fs_lookup_path_at(request.start.dentry?&request.start:NULL,
                              request.root.dentry?&request.root:NULL,
                              request.pathname,!(flags&AT_SYMLINK_NOFOLLOW),
                              &located);
    if(!err)err=nas_vfs_path_node(&located,&node);
    if(!err && nas_current_task) {
        /* access(2) uses REAL ids; faccessat2(AT_EACCESS) uses EFFECTIVE
         * ids, unlike open(2) which uses fsuid/fsgid. Preserve root
         * shortcut and supplementary groups independently. */
        uint32_t uid=(flags&AT_EACCESS) ? TASK.credentials.euid :
                                             TASK.credentials.ruid;
        uint32_t gid=(flags&AT_EACCESS) ? TASK.credentials.egid :
                                             TASK.credentials.rgid;
        struct nas_fs_node fresh={0};
        err=nas_vfs_getattr(&node,&fresh);
        if(!err)node=fresh;
        if(!err && uid) {
            bool group=(gid==node.gid);
            for(uint32_t i=0;i<TASK.credentials.group_count;i++)
                if(TASK.credentials.groups[i]==node.gid)group=true;
            unsigned perm=(uid==node.uid) ? (node.mode>>6)&7U :
                          group ? (node.mode>>3)&7U : node.mode&7U;
            if((perm&mode)!=mode)err=-EACCES;
        }
    }
    if(!err) {
        bits=node.mode;
        is_dir=node.is_dir;
        /* devfs is a read-only NAME namespace, not a read-only device
         * backend: access(W_OK) on a writable character inode is legal. */
        writable=nas_vfs_can_write(&node) ||
                 (node.mode&0170000U)==0020000U;
        end_mounted_lookup(&request,&located);
    } else if(err!=-ENOENT) {
        end_mounted_lookup(&request,&located);
        return (uint64_t)(int64_t)err;
    }
    /* Synthetic devices lack VFS dentries before devfs exists. Only this
     * explicit compatibility endpoint still needs a printed fallback. */
    char path[256]={0};
    if(err==-ENOENT) {
        /* Only a failed lookup reaches this explicit bootstrap whitelist;
         * it must reuse the SAME still-owned request/anchor. */
        bool allowed=nas_fs_bootstrap_candidate(&request,path);
        bool genuinely_missing=allowed &&
            nas_fs_bootstrap_nofollow_probe(&request,request.pathname)==-ENOENT;
        end_mounted_lookup(&request,&located);
        if(!genuinely_missing)return (uint64_t)-ENOENT;
        struct nas_fs_bootstrap_info bootstrap;
        if(nas_fs_bootstrap_describe(path,&bootstrap))return (uint64_t)-ENOENT;
        /* Synthetic bootstrap path has no pinned inode or auditable
         * owner. Do not give nonroot an unprotected fallback. */
        if(nas_dac_nonroot())return (uint64_t)-EACCES;
        bits=(uint32_t)bootstrap.mode;
        writable=bootstrap.fd_kind!=FD_BOOTFS;
    }
    if(!bits)return (uint64_t)-ENOENT;
    if((mode & 1ULL) && !(bits & 0111U))return (uint64_t)-EACCES;
    if((mode & 2ULL) && !writable)return (uint64_t)-EROFS;
    if((mode & 2ULL) && !is_dir && (bits&0170000U)==0040000U)
        return (uint64_t)-EACCES;
    return 0;
}
/* A shared open-file description owns the inode across rename/unlink.
 * No tempfs-specific FD or inode lookup belongs in the Linux ABI. */
static uint64_t sys_fchmod_fd(uint64_t fd,uint64_t mode) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    return (uint64_t)(int64_t)nas_vfs_chmod(&opened->vnode,mode);
}
static uint64_t sys_chmod_path(uint64_t dirfd,uint64_t filename,
                               uint64_t mode,uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    const uint64_t AT_EMPTY_PATH=0x1000ULL;
    if(flags & ~(AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    uint8_t first=0;
    if(!filename || !nas_user_read_bytes(filename,&first,1))
        return (uint64_t)-EFAULT;
    if(!first && (flags&AT_EMPTY_PATH)) {
        int64_t fd=nas_linux_signed_dirfd(dirfd);
        if(fd==-100) {
            if(TASK.fs->cwd_pinned)
                return (uint64_t)(int64_t)nas_vfs_chmod(&TASK.fs->cwd_node,mode);
            struct nas_fs_node cwd;
            char canonical[256];
            int rc=nas_fs_lookup_node(TASK.fs->cwd,true,&cwd,canonical);
            return (uint64_t)(int64_t)(rc?rc:nas_vfs_chmod(&cwd,mode));
        }
        if(fd<0 || (uint64_t)fd>=NAS_FD_LIMIT ||
           TASK.fds->entries[fd].kind==FD_CLOSED)
            return (uint64_t)-EBADF;
        const struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[fd].slot];
        /* Unlike fchmod(2), AT_EMPTY_PATH also accepts O_PATH handles. */
        if(!opened->vnode_pinned)return (uint64_t)-EBADF;
        return (uint64_t)(int64_t)nas_vfs_chmod(&opened->vnode,mode);
    }
    if(!first)return (uint64_t)-ENOENT;
    struct nas_fs_path_request request;
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    int rc=nas_fs_user_lookup_begin(dirfd,filename,
                                    !(flags&AT_SYMLINK_NOFOLLOW),
                                    &request,&path,&node);
    if(rc)return (uint64_t)(int64_t)rc;
    /* Linux filesystems commonly cannot change a symlink's own mode.
     * Never silently chmod its referent when NOFOLLOW was requested. */
    rc=(node.is_symlink && (flags&AT_SYMLINK_NOFOLLOW)) ?
       -EOPNOTSUPP : nas_vfs_chmod(&node,mode);
    end_mounted_lookup(&request,&path);
    return (uint64_t)(int64_t)rc;
}
/* Linux chown/fchown/fchownat share one opened-inode VFS operation. Current
 * NaSeed credentials are root; providers must still perform an ACTUAL owner
 * mutation (or reject it) and must never report success for missing support.
 * uid_t/gid_t are 32-bit even on the supported LP64 syscall ABIs; each
 * 0xffffffff value means leave that field unchanged. */
static uint64_t sys_fchown_fd(uint64_t fd,uint64_t uid,uint64_t gid) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    return (uint64_t)(int64_t)nas_vfs_chown(&opened->vnode,
                                             (uint32_t)uid,(uint32_t)gid);
}
static uint64_t sys_chown_path(uint64_t dirfd,uint64_t pathname,
                               uint64_t uid,uint64_t gid,uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    const uint64_t AT_EMPTY_PATH=0x1000ULL;
    if(flags & ~(AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH))return (uint64_t)-EINVAL;
    uint8_t first=0;
    if(!pathname || !nas_user_read_bytes(pathname,&first,1))
        return (uint64_t)-EFAULT;
    if(!first && (flags&AT_EMPTY_PATH)) {
        int64_t fd=nas_linux_signed_dirfd(dirfd);
        if(fd==-100) {
            if(TASK.fs->cwd_pinned)
                return (uint64_t)(int64_t)nas_vfs_chown(&TASK.fs->cwd_node,
                                           (uint32_t)uid,(uint32_t)gid);
            struct nas_fs_node cwd;
            char canonical[256];
            int rc=nas_fs_lookup_node(TASK.fs->cwd,true,&cwd,canonical);
            return (uint64_t)(int64_t)(rc?rc:nas_vfs_chown(&cwd,
                                           (uint32_t)uid,(uint32_t)gid));
        }
        if(fd<0 || (uint64_t)fd>=NAS_FD_LIMIT ||
           TASK.fds->entries[fd].kind==FD_CLOSED)
            return (uint64_t)-EBADF;
        const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
        if(!opened->vnode_pinned)return (uint64_t)-EBADF;
        /* Unlike fchown(2), AT_EMPTY_PATH accepts a pinned O_PATH inode. */
        return (uint64_t)(int64_t)nas_vfs_chown(&opened->vnode,
                                           (uint32_t)uid,(uint32_t)gid);
    }
    if(!first)return (uint64_t)-ENOENT;
    struct nas_fs_path_request request;
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    int rc=nas_fs_user_lookup_begin(dirfd,pathname,
                                    !(flags&AT_SYMLINK_NOFOLLOW),
                                    &request,&path,&node);
    if(rc)return (uint64_t)(int64_t)rc;
    rc=nas_vfs_chown(&node,(uint32_t)uid,(uint32_t)gid);
    end_mounted_lookup(&request,&path);
    return (uint64_t)(int64_t)rc;
}
/* Linux utimensat: native 64-bit timespec ABI common to all three ISAs.
 * The VFS resolves one real inode; CPIO never becomes writable. None of
 * this logic belongs in an architecture or GNU Make special-case handler.
 * UTIME_NOW and UTIME_OMIT have their real Linux values. */
static uint64_t sys_utimensat_file(uint64_t dirfd, uint64_t pathname,
                                   uint64_t timesptr, uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    const uint64_t AT_EMPTY_PATH=0x1000ULL;
    const uint64_t UTIME_NOW=0x3fffffffULL;
    const uint64_t UTIME_OMIT=0x3ffffffeULL;
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    struct nas_fs_node vnode={0};
    struct nas_fs_path_request request;
    struct nas_vfs_path owned={0};
    if (!pathname) {
        int64_t fd=nas_linux_signed_dirfd(dirfd);
        if(fd<0 || (uint64_t)fd>=NAS_FD_LIMIT ||
           TASK.fds->entries[fd].kind==FD_CLOSED)
            return (uint64_t)-EBADF;
        const struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
        if(!opened->vnode_pinned)return (uint64_t)-EBADF;
        vnode=opened->vnode;
    } else {
        int err=nas_fs_user_lookup_begin(dirfd,pathname,
                                         !(flags&AT_SYMLINK_NOFOLLOW),
                                         &request,&owned,&vnode);
        if (err) return (uint64_t)(int64_t)err;
        if (!nas_vfs_can_write(&vnode)) {
            end_mounted_lookup(&request,&owned);
            return (uint64_t)-EROFS;
        }
    }
    uint64_t now=0;
    if (!nas_time_realtime_ns(&now)) now=nas_arch_monotonic_ns();
    uint64_t ns[2]={now,now};
    bool set[2]={true,true};
    if (timesptr) {
        int64_t raw[4];
        if (!nas_user_read_bytes(timesptr,(uint8_t *)raw,sizeof(raw))) {
            if(owned.dentry)end_mounted_lookup(&request,&owned);
            return (uint64_t)-EFAULT;
        }
        for (unsigned i=0;i<2;i++) {
            uint64_t nsec=(uint64_t)raw[i*2+1];
            if (nsec==UTIME_OMIT) { set[i]=false; continue; }
            if (nsec==UTIME_NOW) continue;
            if (raw[i*2]<0 || raw[i*2]>INT64_MAX/(int64_t)NAS_NSEC_PER_SEC ||
                raw[i*2+1]<0 || nsec>=NAS_NSEC_PER_SEC) {
                if(owned.dentry)end_mounted_lookup(&request,&owned);
                return (uint64_t)-EINVAL;
            }
            ns[i]=(uint64_t)raw[i*2]*NAS_NSEC_PER_SEC+nsec;
        }
    }
    int rc=nas_vfs_utimens(&vnode,ns[0],ns[1],set[0],set[1]);
    if(owned.dentry)end_mounted_lookup(&request,&owned);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_fstatat_boot(uint64_t dirfd, uint64_t pathname,
                                 uint64_t statbuf, uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW = 0x100ULL;
    /* Linux accepts AT_NO_AUTOMOUNT as a no-op; glib's fallback from
     * statx(ENOSYS) can still pass it to newfstatat. Accept the flag here,
     * preserving the VFS lookup and symlink-following policy unchanged. */
    const uint64_t AT_NO_AUTOMOUNT = 0x800ULL;
    const uint64_t AT_EMPTY_PATH = 0x1000ULL;
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT | AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    uint8_t first;
    if (!nas_user_read_bytes(pathname, &first, 1)) return (uint64_t)-EFAULT;
    if (first == 0 && (flags & AT_EMPTY_PATH)) {
        int64_t fd=nas_linux_signed_dirfd(dirfd);
        if(fd==-100) {
            /* Linux AT_FDCWD + AT_EMPTY_PATH refers to the current
             * directory's inode, not an invalid file descriptor. */
            if(TASK.fs->cwd_pinned)
                return write_mounted_stat(&TASK.fs->cwd_node,statbuf);
            struct nas_fs_node cwd;
            char canonical[256];
            int err=nas_fs_lookup_node(TASK.fs->cwd,true,&cwd,canonical);
            return err ? (uint64_t)(int64_t)err :
                         write_mounted_stat(&cwd,statbuf);
        }
        return fd<0 ? (uint64_t)-EBADF :
                      sys_fstat_boot((uint64_t)fd,statbuf);
    }
    struct nas_fs_path_request request;
    struct nas_vfs_path path_ref={0};
    struct nas_fs_node node={0};
    int err=nas_fs_user_path_begin(dirfd,pathname,&request);
    if(err)return (uint64_t)(int64_t)err;
    err=nas_fs_lookup_path_at(request.start.dentry?&request.start:NULL,
                              request.root.dentry?&request.root:NULL,
                              request.pathname,!(flags&AT_SYMLINK_NOFOLLOW),
                              &path_ref);
    if(!err)err=nas_vfs_path_node(&path_ref,&node);
    if(!err) {
        uint64_t result=write_mounted_stat(&node,statbuf);
        end_mounted_lookup(&request,&path_ref);
        return result;
    }
    if(err!=-ENOENT) {
        end_mounted_lookup(&request,&path_ref);
        return (uint64_t)(int64_t)err;
    }
    /* Only the SAME explicit bootstrap endpoints allowed by openat may
     * use a printed-path fallback. In particular, ENOENT under a stale
     * renamed/unlinked dirfd must not attempt a second legacy lookup. */
    char path[256];
    bool allowed=nas_fs_bootstrap_candidate(&request,path);
    bool genuinely_missing=allowed &&
        nas_fs_bootstrap_nofollow_probe(&request,request.pathname)==-ENOENT;
    end_mounted_lookup(&request,&path_ref);
    if(!genuinely_missing)return (uint64_t)-ENOENT;
    if(nas_dac_nonroot())return (uint64_t)-EACCES;
    /* stat is a metadata operation. It must not consume a descriptor,
     * initialize a driver, or fail EMFILE simply because FDs are full. */
    struct nas_fs_bootstrap_info bootstrap;
    int described=nas_fs_bootstrap_describe(path,&bootstrap);
    return described ? (uint64_t)(int64_t)described :
                       write_bootstrap_stat(&bootstrap,statbuf);
}

/* Linux statx(2) first 0xa0 bytes plus reserved tail. A real metadata
 * conversion, not an ENOSYS or zeroed success stub: returned inode/type,
 * links, size, timestamps and mount-device match fstat/newfstatat exactly.
 * Birth time is not advertised because VFS providers do not expose it. */
static uint32_t linux_dev_major(uint64_t dev) {
    return (uint32_t)(((dev>>8)&0xfffU)|((dev>>32)&~0xfffULL));
}
static uint32_t linux_dev_minor(uint64_t dev) {
    return (uint32_t)((dev&0xffU)|((dev>>12)&~0xffULL));
}
static uint64_t write_linux_statx(const struct nas_linux_stat *st,
                                  uint64_t destination) {
    struct nas_linux_statx sx={0};
    sx.mask=0x7ffU; /* STATX_BASIC_STATS, BTime deliberately unavailable. */
    sx.blksize=(uint32_t)st->blksize;
    sx.nlink=(uint32_t)st->nlink;
    sx.uid=st->uid;sx.gid=st->gid;
    sx.mode=(uint16_t)st->mode;
    sx.ino=st->ino;sx.size=(uint64_t)st->size;
    sx.blocks=(uint64_t)st->blocks;
    sx.atime.sec=st->atime_sec;sx.atime.nsec=(uint32_t)st->atime_nsec;
    sx.mtime.sec=st->mtime_sec;sx.mtime.nsec=(uint32_t)st->mtime_nsec;
    sx.ctime.sec=st->ctime_sec;sx.ctime.nsec=(uint32_t)st->ctime_nsec;
    sx.dev_major=linux_dev_major(st->dev);
    sx.dev_minor=linux_dev_minor(st->dev);
    sx.rdev_major=linux_dev_major(st->rdev);
    sx.rdev_minor=linux_dev_minor(st->rdev);
    return nas_user_write_bytes(destination,(const uint8_t *)&sx,sizeof(sx))
        ? 0:(uint64_t)-EFAULT;
}
static uint64_t write_mounted_statx(const struct nas_fs_node *node,
                                    uint64_t destination) {
    struct nas_linux_stat st;
    int rc=fill_mounted_stat(node,&st);
    return rc ? (uint64_t)(int64_t)rc : write_linux_statx(&st,destination);
}
static uint64_t write_bootstrap_statx(const struct nas_fs_bootstrap_info *info,
                                      uint64_t destination) {
    struct nas_linux_stat st;
    fill_bootstrap_stat(info,&st);
    return write_linux_statx(&st,destination);
}
/* statx extends fstatat with a uniform 256-byte ABI and a separate mask.
 * The root/dirfd lookup and symlink policy are the existing VFS path API.
 * AT_STATX_SYNC_FORCE/DONT_SYNC are coherent no-ops: NaSeed has no delayed
 * metadata cache. Conflicting sync flags are invalid per Linux ABI. */
static uint64_t sys_statx_boot(uint64_t dirfd, uint64_t pathname,
                                uint64_t flags, uint64_t mask,
                                uint64_t statbuf) {
    const uint64_t AT_SYMLINK_NOFOLLOW=0x100ULL;
    const uint64_t AT_NO_AUTOMOUNT=0x800ULL;
    const uint64_t AT_EMPTY_PATH=0x1000ULL;
    const uint64_t AT_STATX_SYNC_TYPE=0x6000ULL;
    if(flags & ~(AT_SYMLINK_NOFOLLOW|AT_NO_AUTOMOUNT|
                 AT_EMPTY_PATH|AT_STATX_SYNC_TYPE))return (uint64_t)-EINVAL;
    if((flags&AT_STATX_SYNC_TYPE)==AT_STATX_SYNC_TYPE ||
       (mask&0x80000000ULL))return (uint64_t)-EINVAL;
    uint8_t first;
    if(!nas_user_read_bytes(pathname,&first,1))return (uint64_t)-EFAULT;
    if(first==0 && (flags&AT_EMPTY_PATH)) {
        int64_t fd=nas_linux_signed_dirfd(dirfd);
        if(fd==-100) {
            if(TASK.fs->cwd_pinned)
                return write_mounted_statx(&TASK.fs->cwd_node,statbuf);
            struct nas_fs_node cwd;
            char canonical[256];
            int rc=nas_fs_lookup_node(TASK.fs->cwd,true,&cwd,canonical);
            return rc ? (uint64_t)(int64_t)rc :
                        write_mounted_statx(&cwd,statbuf);
        }
        if(fd<0 || (uint64_t)fd>=NAS_FD_LIMIT ||
           TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
        const struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[fd].slot];
        if(opened->vnode_pinned)return write_mounted_statx(&opened->vnode,statbuf);
        if(opened->boot_identity.valid) {
            const struct nas_fs_bootstrap_info info={
                .dev=opened->boot_identity.dev,.ino=opened->boot_identity.ino,
                .rdev=opened->boot_identity.rdev,.mode=opened->boot_identity.mode,
                .size=opened->boot_identity.size
            };
            return write_bootstrap_statx(&info,statbuf);
        }
        return (uint64_t)-EOPNOTSUPP;
    }
    struct nas_fs_path_request request;
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    int rc=nas_fs_user_path_begin(dirfd,pathname,&request);
    if(rc)return (uint64_t)(int64_t)rc;
    rc=nas_fs_lookup_path_at(request.start.dentry?&request.start:NULL,
                              request.root.dentry?&request.root:NULL,
                              request.pathname,!(flags&AT_SYMLINK_NOFOLLOW),
                              &path);
    if(!rc)rc=nas_vfs_path_node(&path,&node);
    if(!rc) {
        uint64_t result=write_mounted_statx(&node,statbuf);
        end_mounted_lookup(&request,&path);
        return result;
    }
    if(rc!=-ENOENT) {
        end_mounted_lookup(&request,&path);
        return (uint64_t)(int64_t)rc;
    }
    char bootstrap_path[256];
    bool allowed=nas_fs_bootstrap_candidate(&request,bootstrap_path);
    bool missing=allowed &&
        nas_fs_bootstrap_nofollow_probe(&request,request.pathname)==-ENOENT;
    end_mounted_lookup(&request,&path);
    if(!missing)return (uint64_t)-ENOENT;
    if(nas_dac_nonroot())return (uint64_t)-EACCES;
    struct nas_fs_bootstrap_info info;
    int described=nas_fs_bootstrap_describe(bootstrap_path,&info);
    return described ? (uint64_t)(int64_t)described :
                       write_bootstrap_statx(&info,statbuf);
}

/* Linux x86-64 linux_dirent64: ino64, off64, reclen16, dtype8, name[].
 * The offset cookie indexes '.', '..', then direct children of a directory.
 */
/* Linux readlink/readlinkat return the link bytes without a terminating NUL,
 * and deliberately do not follow the last component. */
static uint64_t sys_readlinkat_boot(uint64_t dirfd, uint64_t pathname,
                                     uint64_t dst, uint64_t capacity) {
    if (!capacity) return (uint64_t)-EINVAL;
    struct nas_fs_path_request request;
    struct nas_vfs_path path={0};
    struct nas_fs_node node={0};
    int error=nas_fs_user_lookup_begin(dirfd,pathname,false,
                                       &request,&path,&node);
    if(error)return (uint64_t)(int64_t)error;
    if((node.mode&0170000U)!=0120000U) {
        end_mounted_lookup(&request,&path);
        return (uint64_t)-EINVAL;
    }
    char target[256];
    int got=nas_vfs_readlink(&node,target);
    if(got<0) {
        end_mounted_lookup(&request,&path);
        return (uint64_t)(int64_t)got;
    }
    uint64_t size=(uint64_t)got<capacity?(uint64_t)got:capacity;
    uint64_t result=nas_user_write_bytes(dst,(const uint8_t *)target,size)
        ? size:(uint64_t)-EFAULT;
    end_mounted_lookup(&request,&path);
    return result;
}

/* Translate any mounted provider readdir callback to Linux dirent64,
 * without exposing host/ext4 structures or accessing user memory in drivers.
 * The logical cookie resides in the shared open File, not in a task FD. */
static uint64_t vfs_getdents64(uint64_t fd,uint64_t user,uint64_t capacity) {
    struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if(!opened->vnode.is_dir)return (uint64_t)-ENOTDIR;
    if(capacity<24)return (uint64_t)-EINVAL;
    if(user>=USER_LIMIT || capacity>USER_LIMIT-user)return (uint64_t)-EFAULT;
    uint64_t cursor=opened->position,written=0;
    struct nas_vfs_dirent batch[8];
    while(capacity-written>=24) {
        int count=nas_vfs_read_dir(&opened->vnode,cursor,batch,8);
        if(count<0)return written?written:(uint64_t)(int64_t)count;
        if(!count)break;
        if(count>8)return written?written:(uint64_t)-EIO;
        for(int i=0;i<count;i++) {
            const struct nas_vfs_dirent *e=&batch[i];
            unsigned length=e->name_len;
            if(!e->ino || !length ||
               e->name[length]!=0)return written?written:(uint64_t)-EIO;
            unsigned record_bytes=(19+length+1+7)&~7U;
            if(record_bytes>280)return written?written:(uint64_t)-EIO;
            if(record_bytes>capacity-written)
                return written?written:(uint64_t)-EINVAL;
            uint8_t raw[280]={0};
            for(unsigned b=0;b<8;b++) {
                raw[b]=(uint8_t)(e->ino>>(b*8));
                raw[8+b]=(uint8_t)((cursor+1)>>(b*8));
            }
            raw[16]=(uint8_t)record_bytes;
            raw[17]=(uint8_t)(record_bytes>>8);
            raw[18]=e->dtype;
            memcpy(raw+19,e->name,length);
            if(!nas_user_write_bytes(user+written,raw,record_bytes))
                return written?written:(uint64_t)-EFAULT;
            written+=record_bytes;
            opened->position=++cursor;
        }
    }
    return written;
}
static uint64_t sys_getdents64(uint64_t fd,uint64_t dst,uint64_t capacity) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
        return (uint64_t)-EBADF;
    const struct nas_open_file *file=
        &nas_open_files[TASK.fds->entries[fd].slot];
    if(!file->vnode_pinned || !file->vnode.is_dir)
        return (uint64_t)-ENOTDIR;
    return vfs_getdents64(fd,dst,capacity);
}

#define METADATA_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_metadata_syscall(enum nas_syscall_op op,
                                            const uint64_t *args) {
    switch (op) {
    case NAS_SC_chmod: METADATA_RESULT(sys_chmod_path((uint64_t)-100,args[1],args[2],0));
    case NAS_SC_fchmod: METADATA_RESULT(sys_fchmod_fd(args[1],args[2]));
    case NAS_SC_fchmodat: METADATA_RESULT(sys_chmod_path(args[1],args[2],args[3],0));
    case NAS_SC_fchmodat2: METADATA_RESULT(sys_chmod_path(args[1],args[2],args[3],args[4]));
    case NAS_SC_chown: METADATA_RESULT(sys_chown_path((uint64_t)-100,args[1],args[2],args[3],0));
    case NAS_SC_lchown: METADATA_RESULT(sys_chown_path((uint64_t)-100,args[1],args[2],args[3],0x100ULL));
    case NAS_SC_fchown: METADATA_RESULT(sys_fchown_fd(args[1],args[2],args[3]));
    case NAS_SC_fchownat: METADATA_RESULT(sys_chown_path(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_utimensat: METADATA_RESULT(sys_utimensat_file(args[1],args[2],args[3],args[4]));
    case NAS_SC_access: METADATA_RESULT(sys_access_path((uint64_t)-100,args[1],args[2],0));
    case NAS_SC_faccessat: METADATA_RESULT(sys_access_path(args[1],args[2],args[3],0));
    case NAS_SC_faccessat2: METADATA_RESULT(sys_access_path(args[1],args[2],args[3],args[4]));
    case NAS_SC_fstat: METADATA_RESULT(sys_fstat_boot(args[1], args[2]));
    case NAS_SC_fstatfs: METADATA_RESULT(sys_fstatfs_boot(args[1],args[2]));
    case NAS_SC_newfstatat: METADATA_RESULT(sys_fstatat_boot(args[1], args[2], args[3], args[4]));
    case NAS_SC_statx: METADATA_RESULT(sys_statx_boot(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_stat: METADATA_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0));
    case NAS_SC_statfs: METADATA_RESULT(sys_statfs_boot(args[1],args[2]));
    case NAS_SC_lstat: METADATA_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0x100ULL));
    case NAS_SC_lseek: METADATA_RESULT(sys_lseek_boot(args[1], args[2], args[3]));
    case NAS_SC_fsync: case NAS_SC_fdatasync: METADATA_RESULT(sys_sync_fd(args[1]));
    case NAS_SC_sync: METADATA_RESULT((uint64_t)(int64_t)nas_vfs_sync_all());
    case NAS_SC_ftruncate: METADATA_RESULT(sys_resize_fd(args[1],args[2]));
    case NAS_SC_truncate: METADATA_RESULT(sys_resize_path(args[1],args[2]));
    case NAS_SC_fadvise64: METADATA_RESULT(sys_fadvise_fd(args[1],args[2],args[3],args[4]));
    case NAS_SC_fallocate: METADATA_RESULT(sys_preallocate_fd(args[1],args[2],args[3],args[4]));
    case NAS_SC_getdents64: METADATA_RESULT(sys_getdents64(args[1], args[2], args[3]));
    case NAS_SC_readlink: METADATA_RESULT(sys_readlinkat_boot((uint64_t)-100, args[1], args[2], args[3]));
    case NAS_SC_readlinkat: METADATA_RESULT(sys_readlinkat_boot(args[1], args[2], args[3], args[4]));
    default: return (struct nas_fs_result){ .handled=false, .value=0 };
    }
}
#undef METADATA_RESULT
