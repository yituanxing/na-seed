#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../syscall/ops.h"
#include "../task.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../pipe.h"
#include "../path.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/arch.h"
#include "../arch/abi.h"
#include "../arch/linux_stat_abi.h"
#include "../arch/linux_fcntl_abi.h"
#include "../arch/mmu.h"
#include "../drivers/tty.h"
#include "../drivers/pty.h"
#include "io.h"
#include "mode.h"
#include "syscall.h"
#include "procfs.h"
#include "file_ops.h"
#include "posix_locks.h"
#include "epoll.h"
#include "eventfd.h"
#include "unix_socket.h"
#include "namespace_syscall.h"
#include "socket_syscall.h"
#include "metadata_syscall.h"
#include "open_path.h"
#include "path_request.h"
#include "lookup.h"
#include "vfs.h"
#include "../time/clock.h"
/* Linux x86-64 filesystem syscall adapter. Backend state is already
 * task-local or refcounted via fd.c, pipe.c, bootfs.c and tmpfs.c.
 * The user ABI stat/dirent encodings are still x86-specific in v1. */
#define TASK (*nas_current_task)
#define current_workdir TASK.fs->cwd
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine
#define saw_bash_hello TASK.saw_bash_hello
#define matched_chars TASK.matched_chars
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_putchar(char);
extern const struct limine_file *nas_module(uint64_t);
static void nas_fs_fail(const char *msg) __attribute__((noreturn));
static void nas_fs_fail(const char *msg) {
    nas_print("[FAIL] M1 "); nas_print(msg); nas_print("\n");
    nas_arch_halt();
}

static void observe_stdout(char ch) {
    static const char text[] = "hello\n";
    if (ch == text[matched_chars]) {
        if (++matched_chars == sizeof(text) - 1) {
            saw_bash_hello = true;
            matched_chars = 0;
        }
    } else matched_chars = (ch == text[0]) ? 1 : 0;
}



/* User-copy contract now declared in mm/usercopy.h. */
/* Single-process POSIX descriptors over actual UART and device files.
 * No fake TTY, disk, /proc, or filesystem contents are supplied.
 */
extern void nas_putchar(char c);

/* Per-FD handles share a refcounted open-file description after dup().
 * All fd aliases must observe the same read/seek/directory cursor.
 */
/* Read-only boot filesystem backed by bytes of the actual Limine modules.
 * Paths name exactly the binaries the bootloader provided, not fake files.
 */

/* Pipe ring/object lifetime is private to pipe.c. The syscall layer
 * assigns per-task FDs and validates user buffers before copying bytes.
 */
/* FD bookkeeping is syscall-local, but inode lifetime and slot identity now
 * live in the dedicated RAM /tmp backend, with shared file descriptions. */
 /* mmap regular file reads live in file_io.c */
/* The two endpoints are one pipe object, with separate shared-description
 * slots. Allocation remains transactional until both FD/slot pairs are
 * available; only then is the backend created. */
static uint64_t sys_channel_pair(uint64_t user_pair, uint64_t flags,
                                 bool stream_socket) {
    if (user_pair >= USER_LIMIT || user_pair > USER_LIMIT - 8)
        return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < 8; i++)
        if (!nas_mmu_user_byte(user_pair + i)) return (uint64_t)-EFAULT;
    unsigned fds[2], slots[2];
    int capacity=nas_fd_reserve(nas_current_task,0,2,fds,slots,
                                stream_socket ? "socketpair" : "pipe2");
    if(capacity)return (uint64_t)(int64_t)capacity;
    int result = stream_socket ?
        nas_pipe_create_socketpair(slots[0], slots[1]) :
        nas_pipe_create(slots[0], slots[1]);
    if (result) return (uint64_t)(int64_t)result;
    if (stream_socket) nas_unix_socket_pair_ready(slots[0],slots[1]);
    for (unsigned i = 0; i < 2; i++) {
        unsigned fd = fds[i], slot = slots[i];
        uint8_t kind=stream_socket ? FD_SOCKET : i ? FD_PIPE_W : FD_PIPE_R;
        uint64_t status=(stream_socket ? 2ULL : i ? 1ULL : 0ULL) |
                        (flags & 0x800ULL);
        if(!nas_fd_install_new(nas_current_task,fd,slot,kind,status,
                               !!(flags & 0x80000ULL)))
            nas_fs_fail("reserved pipe/socket FD was occupied");
    }
    uint8_t output[8] = {0};
    for (unsigned i = 0; i < 2; i++)
        for (unsigned j = 0; j < 4; j++)
            output[i * 4 + j] = (uint8_t)(fds[i] >> (j * 8));
    if (!nas_user_write_bytes(user_pair, output, sizeof(output)))
        nas_fs_fail("pipe2 validated destination became inaccessible");
    nas_print(stream_socket ? "[SOCKETPAIR] created a=" : "[PIPE] created read=");
    nas_dec(fds[0]);
    nas_print(stream_socket ? " b=" : " write=");
    nas_dec(fds[1]); nas_print("\n");
    return 0;
}
static uint64_t sys_pipe2(uint64_t user_pair,uint64_t flags) {
    if(flags & ~(0x800ULL|0x80000ULL))return (uint64_t)-EINVAL;
    return sys_channel_pair(user_pair,flags,false);
}
static uint64_t sys_socketpair(uint64_t domain,uint64_t type,
                               uint64_t protocol,uint64_t user_pair) {
    /* Real AF_UNIX SOCK_STREAM IPC only; never claim generic networking.
     * SOCK_NONBLOCK/SOCK_CLOEXEC share the existing open-file semantics. */
    if(domain!=1)return (uint64_t)-EAFNOSUPPORT;
    if((type & 15U)!=1U || (type & ~(15ULL|0x800ULL|0x80000ULL)))
        return (uint64_t)-ESOCKTNOSUPPORT;
    if(protocol)return (uint64_t)-EPROTONOSUPPORT;
    return sys_channel_pair(user_pair,type & (0x800ULL|0x80000ULL),true);
}
/* AF_UNIX Linux syscall ABI lives in socket_syscall.c. */
static uint64_t pipe_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    unsigned slot = TASK.fds->entries[fd].slot;
    int64_t available = nas_pipe_read_amount(
        slot, length, !!(nas_open_files[slot].status_flags & 0x800ULL));
    if (available <= 0) return (uint64_t)available;
    unsigned count = (unsigned)available, n = 0;
    for (; n < count; n++)
        if (!nas_mmu_user_byte(address + n)) break;
    if (!n) return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < n; i++) {
        uint8_t value = nas_pipe_read_byte(slot, i);
        if (!nas_user_write_bytes(address + i, &value, 1))
            nas_fs_fail("pipe read validated user mapping became inaccessible");
    }
    nas_pipe_read_commit(slot, n);
    return n;
}
static uint64_t pipe_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    unsigned slot = TASK.fds->entries[fd].slot;
    int64_t available = nas_pipe_write_amount(
        slot, length, !!(nas_open_files[slot].status_flags & 0x800ULL));
    if (available <= 0) return (uint64_t)available;
    unsigned count = (unsigned)available, n = 0;
    for (; n < count; n++) {
        uint8_t *src = nas_mmu_user_byte(address + n);
        if (!src) break;
        nas_pipe_write_byte(slot, n, *src);
    }
    if (!n) return (uint64_t)-EFAULT;
    nas_pipe_write_commit(slot, n);
    return n;
}

/* FD table aliases and shared open descriptions are owned by fd.c.
 * Release the pipe and RAM-inode backends only after the last reference
 * disappears. dup2 must close an existing destination through this hook.
 */
/* Canonical backend finalization: process closes AND kernel-owned rights. */
void nas_fs_description_final_close(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || nas_open_files[slot].refcount)
        nas_fs_fail("closing nonfinal open description");
    nas_epoll_target_final_close(slot);
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    if(ops && ops->release)ops->release(slot);
    /* flock belongs to the SHARED File, not the current task's FD.
     * dup/fork/SCM_RIGHTS release this lock only on final close. */
    nas_vfs_release_flock(&nas_open_files[slot]);
    if(nas_open_files[slot].vpath.dentry &&
       nas_vfs_path_put(&nas_open_files[slot].vpath))
        nas_fs_fail("File dentry pin corrupted at final close");
    if(nas_open_files[slot].vnode_pinned &&
       nas_vfs_node_unpin(&nas_open_files[slot].vnode))
        nas_fs_fail("File mount pin corrupted at final close");
    nas_open_files[slot]=(struct nas_open_file){0};
}
void nas_fs_drop_kernel_description(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        nas_fs_fail("kernel description refcount corrupted");
    if(!--nas_open_files[slot].refcount)
        nas_fs_description_final_close(slot);
}
void nas_fs_close_fd(uint64_t fd) {
    /* Linux POSIX locks are PROCESS-owned and closing even one descriptor
     * releases that process's locks for the whole inode (including dup). */
    if(fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind!=FD_CLOSED) {
        const struct nas_open_file *open=
            &nas_open_files[TASK.fds->entries[fd].slot];
        if(open->vnode_pinned)
            nas_vfs_release_posix_locks(&open->vnode,
                                        nas_task_process_pid(nas_current_task));
    }
    unsigned final_slot=nas_fd_close(nas_current_task, (unsigned)fd);
    if (final_slot == NAS_OPEN_FILE_LIMIT) return;
    nas_fs_description_final_close(final_slot);
}
static uint64_t fd_clone(uint64_t src, uint64_t dst, bool cloexec) {
    if (src >= NAS_FD_LIMIT || TASK.fds->entries[src].kind == FD_CLOSED ||
        dst >= NAS_FD_LIMIT) return (uint64_t)-EBADF;
    if (src == dst) return dst;
    nas_fs_close_fd(dst);
    return nas_fd_clone(nas_current_task, src, dst, cloexec);
}
static uint64_t fd_duplicate(uint64_t oldfd, uint64_t minimum,
                             bool cloexec) {
    return nas_fd_duplicate(nas_current_task, oldfd, minimum, cloexec);
}
/* Linux native 64-bit flock ABI (common to x86, arm64 and rv64):
 * short type, short whence, pad32, s64 start, s64 len, s32 pid. */
struct nas_flock64 { int16_t type,whence; uint32_t pad;
                    int64_t start,length; int32_t pid; uint32_t reserved; };
_Static_assert(sizeof(struct nas_flock64)==32,"Linux LP64 flock size");
static uint64_t sys_fcntl_lock(uint64_t fd,uint64_t command,uint64_t user) {
    struct nas_open_file *opened=&nas_open_files[TASK.fds->entries[fd].slot];
    if(opened->status_flags&NAS_LINUX_O_PATH)return (uint64_t)-EBADF;
    if(!opened->vnode_pinned || opened->vnode.is_dir)
        return (uint64_t)-EBADF;
    struct nas_posix_locks *locks=nas_vfs_posix_locks(&opened->vnode);
    if(!locks)return (uint64_t)-EBADF;
    struct nas_flock64 lk={0};
    if(!nas_user_read_bytes(user,(uint8_t *)&lk,sizeof(lk)))
        return (uint64_t)-EFAULT;
    if(lk.type<0 || lk.type>2 || lk.whence<0 || lk.whence>2)
        return (uint64_t)-EINVAL;
    uint64_t base=0;
    if(lk.whence==1)base=opened->position;
    if(lk.whence==2)base=nas_vfs_node_size(&opened->vnode);
    if(base>INT64_MAX)return (uint64_t)-EOVERFLOW;
    /* POSIX permits negative l_len; this first fixed-range VFS refuses
     * an unsupported range rather than corrupting another owner's locks. */
    if(lk.length<0)return (uint64_t)-EINVAL;
    if(lk.start<0 && (uint64_t)(-lk.start)>base)return (uint64_t)-EINVAL;
    uint64_t start=lk.start<0 ? base-(uint64_t)(-lk.start) : base+(uint64_t)lk.start;
    if(start>INT64_MAX)return (uint64_t)-EOVERFLOW;
    uint64_t end=lk.length==0 ? UINT64_MAX : start+(uint64_t)lk.length;
    if(end<start || end==start)return (uint64_t)-EOVERFLOW;
    if(command!=5) {
        unsigned access=(unsigned)(nas_open_files[TASK.fds->entries[fd].slot].status_flags&3ULL);
        if((lk.type==NAS_LOCK_READ && access==1) ||
           (lk.type==NAS_LOCK_WRITE && access==0))return (uint64_t)-EBADF;
    }
    if(command==5) {
        if(lk.type==NAS_LOCK_UNLOCK)return (uint64_t)-EINVAL;
        int conflict=nas_locks_conflict(locks,nas_task_process_pid(nas_current_task),
                                     (uint16_t)lk.type,start,end);
        if(conflict<0)lk.type=NAS_LOCK_UNLOCK;
        else {
            const struct nas_posix_lock *old=&locks->entries[conflict];
            lk.type=(int16_t)old->type;
            lk.whence=0;
            lk.start=(int64_t)old->start;
            lk.length=old->end==UINT64_MAX ? 0 : (int64_t)(old->end-old->start);
            lk.pid=(int32_t)old->pid;
        }
        return nas_user_write_bytes(user,(uint8_t *)&lk,sizeof(lk)) ?
               0 : (uint64_t)-EFAULT;
    }
    int rc=nas_locks_apply(locks,nas_task_process_pid(nas_current_task),
                           (uint16_t)lk.type,start,end);
    if(rc==1)return (uint64_t)-EAGAIN; /* non-blocking lock conflict */
    if(rc==2)return (uint64_t)-ENOLCK;
    return rc ? (uint64_t)-EINVAL : 0;
}
static uint64_t sys_fcntl(uint64_t fd, uint64_t command, uint64_t arg) {
    if (fd >= 64 || TASK.fds->entries[fd].kind == FD_CLOSED)
        return (uint64_t)-EBADF;
    unsigned slot = TASK.fds->entries[fd].slot;
    switch (command) {
    case 0: return fd_duplicate(fd, arg, false);  /* F_DUPFD */
    case 1: return TASK.fds->entries[fd].cloexec;                 /* F_GETFD */
    case 2: TASK.fds->entries[fd].cloexec = (uint8_t)(arg & 1); return 0; /* F_SETFD */
    case 3: return nas_open_files[slot].status_flags;       /* F_GETFL */
    case 4: /* F_SETFL: only the O_NONBLOCK/O_APPEND mutable subset */
        if(nas_open_files[slot].status_flags&NAS_LINUX_O_PATH)
            return (uint64_t)-EBADF;
        nas_open_files[slot].status_flags =
            (nas_open_files[slot].status_flags & ~(0x800ULL | 0x400ULL)) |
            (arg & (0x800ULL | 0x400ULL));
        return 0;
    case 5: case 6: case 7: /* F_GETLK / F_SETLK / F_SETLKW */
        return sys_fcntl_lock(fd,command,arg);
    case 1030: return fd_duplicate(fd, arg, true); /* F_DUPFD_CLOEXEC */
    default: return (uint64_t)-EINVAL; /* unsupported fcntl cmd */
    }
}
/* Linux FIONBIO is descriptor-independent: libuv/CMake sets it on
 * both ends of pipes, even when pipe2(O_NONBLOCK) was already used.
 * Preserve open-description semantics across dup/fork, as F_SETFL does.
 * FIOCLEX/FIONCLEX, by contrast, belong to the individual descriptor. */
static uint64_t sys_ioctl_fd(uint64_t fd,uint64_t cmd,uint64_t arg) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)return (uint64_t)-EBADF;
    /* Linux ioctl _IOC commands occupy 32 bits even on LP64. Musl may
     * sign-extend an _IOR command (for example TIOCGPTN=0x80045430) when
     * passing it through the 64-bit syscall register on ALL three ISAs.
     * The semantic request is its low 32 bits, not the sign extension. */
    cmd=(uint32_t)cmd;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(cmd==0x5421ULL) { /* FIONBIO: pointer to native int */
        if(nas_open_files[slot].status_flags&NAS_LINUX_O_PATH)
            return (uint64_t)-EBADF;
        uint32_t enabled=0;
        if(!nas_user_read_bytes(arg,(uint8_t *)&enabled,sizeof(enabled)))
            return (uint64_t)-EFAULT;
        if(enabled)nas_open_files[slot].status_flags|=0x800ULL;
        else nas_open_files[slot].status_flags&=~0x800ULL;
        return 0;
    }
    if(cmd==0x5451ULL || cmd==0x5450ULL) { /* FIOCLEX / FIONCLEX */
        TASK.fds->entries[fd].cloexec=(uint8_t)(cmd==0x5451ULL);
        return 0;
    }
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    return ops && ops->ioctl ? ops->ioctl(fd,cmd,arg) : (uint64_t)-ENOTTY;
}

static const struct limine_file *bootfs_lookup(const char *path) {
    if ((strcmp(path, "/bin/busybox") == 0)) return nas_module(0);
    if ((strcmp(path, NAS_ELF_INTERP) == 0) ||
        (strcmp(path, NAS_ELF_LIBC_ALIAS) == 0))
        return nas_module(1);
    return 0;
}

/* Pathname / dirfd user-address adapter lives in namespace_syscall.c. */
/* A bounded adapter transfers user buffers to the shared page-backed tmpfs.
 * Kernel filesystem storage knows nothing about user virtual addresses. */
/* One provider-backed read for every mounted regular File. The filename
 * is resolved at open; seek offsets live in the shared open description. */
static uint64_t vread_regular(uint64_t fd,uint64_t address,uint64_t length) {
    if(!length)return 0;
    if(address>=USER_LIMIT || length>USER_LIMIT-address)
        return (uint64_t)-EFAULT;
    unsigned slot=TASK.fds->entries[fd].slot;
    uint64_t start=nas_open_files[slot].position,done=0;
    uint8_t buffer[1024];
    while(done<length) {
        uint64_t take=length-done;
        if(take>sizeof(buffer))take=sizeof(buffer);
        int64_t got=nas_fs_mmap_read(fd,start+done,buffer,take);
        if(got<0)return done ? done : (uint64_t)got;
        if(!got)break;
        if(!nas_user_write_bytes(address+done,buffer,(uint64_t)got))
            return done ? done : (uint64_t)-EFAULT;
        done+=(uint64_t)got;
        nas_open_files[slot].position=start+done;
        if((uint64_t)got<take)break;
    }
    return done;
}
static uint64_t vwrite_regular(uint64_t fd, uint64_t address, uint64_t length) {
    unsigned slot = TASK.fds->entries[fd].slot;
    struct nas_open_file *opened=&nas_open_files[slot];
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    if(!nas_vfs_can_write(&opened->vnode))return (uint64_t)-EROFS;
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    /* tmpfs has a live inode size; all other descriptors own a shared
     * inode snapshot, updated after successful writes. */
    uint64_t size=nas_vfs_node_size(&opened->vnode);
    uint64_t offset=(opened->status_flags & 0x400ULL) ?
                    size : opened->position;
    uint64_t done = 0;
    uint8_t buffer[1024];
    while (done < length) {
        uint64_t n = 0;
        while (n < sizeof(buffer) && n < length - done) {
            uint8_t *ptr = nas_mmu_user_byte(address + done + n);
            if (!ptr) break;
            buffer[n++] = *ptr;
        }
        if (!n) return done ? done : (uint64_t)-EFAULT;
        int64_t written = nas_vfs_write_at(&opened->vnode,offset+done,buffer,n);
        if (written < 0) return done ? done : (uint64_t)written;
        done += (uint64_t)written;
        if(offset+done>opened->vnode.size)opened->vnode.size=offset+done;
        opened->position=offset+done;
        if ((uint64_t)written != n) break;
        if (n < sizeof(buffer) && done < length) break; /* user fault */
    }
    opened->position=offset+done;
    return done;
}
/* Namespace-changing syscalls live in namespace_syscall.c. */
/* One open(2) control plane: namespace namei selects a VFS inode first,
 * then Linux flags apply to its mode/capabilities. Backend/type-specific
 * behavior belongs to VFS providers, not a parallel tmpfs/procfs branch.
 * Only /dev character endpoints and original unmounted Limine modules
 * remain outside the filesystem namespace until devfs is mounted. */
static uint64_t open_path_owned(const char *path,
                                const struct nas_fs_path_request *request,
                                uint64_t native_flags,uint64_t create_mode) {
    if(!path || (!request && path[0]!='/'))return (uint64_t)-EINVAL;
    uint64_t flags;
    if(!nas_linux_open_flags(native_flags,&flags))
        return (uint64_t)-EINVAL;
    flags=nas_linux_path_only_flags(flags);
    const uint64_t permitted=NAS_LINUX_O_PATH | 3ULL | 0x800ULL | 0x8000ULL |
        0x10000ULL | 0x20000ULL | 0x80000ULL | 0x100ULL | 0x80ULL |
        0x40ULL | 0x200ULL | 0x400ULL;
    if(flags&~permitted)return (uint64_t)-EINVAL;
    const bool path_only=(flags&NAS_LINUX_O_PATH)!=0;
    const bool follow_final=(flags&0x20000ULL)==0; /* O_NOFOLLOW */
    const bool create=(flags&0x40ULL)!=0;          /* O_CREAT */
    const bool exclusive=(flags&0x80ULL)!=0;       /* O_EXCL */
    const bool truncate=(flags&0x200ULL)!=0;      /* O_TRUNC */
    const unsigned access=(unsigned)(flags&3ULL);
    uint8_t kind=FD_CLOSED;
    int pty_number=nas_pty_path_number(path);
    const struct limine_file *real_file=NULL;
    bool have_node=false;
    struct nas_fs_node opened_node={0};
    struct nas_vfs_path directory_path={0};
    char canonical[256]={0};

    /* Temporary synthetic-device bridge; filesystems never receive a
     * fabricated VFS inode, and future devfs can replace this one block. */
    if(!strcmp(path,"/dev/tty") &&
       !nas_tty_controlling() && !nas_pty_controlling())
        return (uint64_t)-ENXIO;
    if(!strcmp(path,"/dev/ptmx"))kind=FD_PTY_MASTER;
    else if(pty_number>=0 && nas_pty_present((unsigned)pty_number))
        kind=FD_PTY_SLAVE;
    else if(!strcmp(path,"/dev/tty") && nas_pty_controlling())
        kind=FD_PTY_SLAVE;
    else if(!strcmp(path,"/dev/tty") || !strcmp(path,"/dev/console") ||
            !strcmp(path,"/dev/ttyS0"))kind=FD_TTY;
    else if(!strcmp(path,"/dev/null"))kind=FD_NULL;
    else if(!strcmp(path,"/dev/zero"))kind=FD_ZERO;

    /* Reserve both resources without publication BEFORE namei may create
     * or truncate a filesystem inode. EMFILE/ENFILE must never cause
     * O_CREAT or O_TRUNC side effects. This probe is non-mutating. */
    unsigned fds[2],slots[2];
    int capacity=nas_fd_reserve(nas_current_task,0,1,fds,slots,"openat");
    if(capacity)return (uint64_t)(int64_t)capacity;
    const unsigned fd=fds[0],slot=slots[0];

    if(kind==FD_CLOSED) {
        /* The VFS owns namei, O_CREAT, inode mode/permission validation,
         * truncate and the returned mount/inode pin as ONE transaction.
         * Linux native flags are decoded here and never enter providers. */
        const struct nas_vfs_open_options options={
            .follow_final=follow_final,.create=create,
            .exclusive=exclusive,.truncate=truncate,.path_only=path_only,
            .directory_only=(flags&0x10000ULL)!=0,.access=access,
            .create_mode=nas_fs_create_mode(create_mode,TASK.fs->umask)
        };
        int rc=request ?
            nas_vfs_open_path_at(request->start.dentry?&request->start:NULL,
                                 request->root.dentry?&request->root:NULL,
                                 path,&options,&opened_node,&directory_path) :
            nas_vfs_open_inode(path,&options,&opened_node,canonical,
                               &directory_path);
        if(!rc) {
            have_node=true;
            kind=opened_node.is_dir?FD_DIR:FD_ARCHIVE;
            if(request && directory_path.dentry)
                (void)nas_vfs_path_string(&directory_path,canonical);
        } else if(rc==-ENOENT && !request && running_alpine &&
                  (real_file=bootfs_lookup(path))) {
            /* Original unmounted Limine module; a mounted VFS inode
             * always wins, never reopen a stale old pathname. */
            kind=FD_BOOTFS;
        } else return (uint64_t)(int64_t)rc;
    }

    if(!have_node) {
        if(flags&0x10000ULL)return (uint64_t)-ENOTDIR;
        if(create && exclusive)return (uint64_t)-EEXIST;
        if(kind==FD_BOOTFS) {
            if(flags&(0x40ULL|0x400ULL))return (uint64_t)-EROFS;
            if(access)return (uint64_t)-EACCES;
        }
    }

    if(kind==FD_PTY_MASTER) {
        int err=nas_pty_new(slot);
        if(err)return (uint64_t)(int64_t)err;
    } else if(kind==FD_PTY_SLAVE) {
        int err=pty_number>=0 ?
            nas_pty_attach((unsigned)pty_number,slot) :
            nas_pty_attach_controlling(slot);
        if(err)return (uint64_t)(int64_t)err;
    }
    /* Inode AND optional directory Dentry are one VFS-owned open
     * transaction. The syscall layer just transfers their references
     * to this shared File description; it never constructs a Path. */
    if(!nas_fd_install_new(nas_current_task,fd,slot,kind,
                           flags&(NAS_LINUX_O_PATH|3ULL|0x800ULL|0x400ULL),
                           !!(flags&0x80000ULL)))
        nas_fs_fail("reserved openat description was occupied");
    if(have_node) {
        nas_open_files[slot].vnode=opened_node;
        nas_open_files[slot].vnode_pinned=true;
        nas_open_files[slot].vpath=directory_path;
    }
    nas_open_files[slot].boot_file=real_file;
    /* Diagnostic hint only. No open/read/stat/dirfd may rely on an old
     * pathname after rename: its pinned VFS inode remains authoritative. */
    const char *origin=have_node && canonical[0]?canonical:path;
    unsigned path_len=0;
    while(origin[path_len] &&
          path_len<sizeof(nas_open_files[slot].origin_path)-1U) {
        nas_open_files[slot].origin_path[path_len]=origin[path_len];
        ++path_len;
    }
    nas_open_files[slot].origin_path[path_len]=0;
    return fd;
}
uint64_t nas_fs_open_path(const char *path,uint64_t native_flags,
                          uint64_t create_mode) {
    return open_path_owned(path,NULL,native_flags,create_mode);
}
/* This is NOT a second namei for ordinary missing paths. In a direct-ELF
 * rootless boot a few synthetic devices and Limine modules have no VFS
 * inode; only these explicit bootstrap endpoints may use the old adapter.
 * In particular ENOENT from a renamed/unlinked directory FD never causes
 * an arbitrary O_CREAT to be retried through a pathname reconstructed from
 * that FD. */
static bool is_bootstrap_open_endpoint(const char *path) {
    if(!path || path[0]!='/')return false;
    if(!strcmp(path,"/dev/null") || !strcmp(path,"/dev/zero") ||
       !strcmp(path,"/dev/tty") || !strcmp(path,"/dev/ttyS0") ||
       !strcmp(path,"/dev/console") || !strcmp(path,"/dev/ptmx"))
        return true;
    int pty=nas_pty_path_number(path);
    if(pty>=0 && nas_pty_present((unsigned)pty))return true;
    return running_alpine && bootfs_lookup(path)!=NULL;
}
static uint64_t maybe_open_bootstrap(const struct nas_fs_path_request *request,
                                     uint64_t dirfd,uint64_t pathname,
                                     uint64_t flags,uint64_t mode,
                                     uint64_t original) {
    if((int64_t)original!=-ENOENT)return original;
    /* Classify using the already owned lookup anchor BEFORE asking the old
     * string adapter anything. Ordinary ENOENT never gets a second namei. */
    char candidate[256];
    if(request->pathname[0]=='/')
        memcpy(candidate,request->pathname,strlen(request->pathname)+1);
    else {
        if(!request->start.dentry)return original;
        char base[256];
        if(nas_vfs_path_string(&request->start,base) ||
           nas_path_join_raw(base,request->pathname,candidate))
            return original;
    }
    if(!is_bootstrap_open_endpoint(candidate))return original;
    char legacy[256];
    uint64_t found=nas_fs_resolve_path(dirfd,pathname,legacy);
    if((int64_t)found<0 || !is_bootstrap_open_endpoint(legacy))
        return original;
    return nas_fs_open_path(legacy,flags,mode);
}
static uint64_t sys_open_device(uint64_t pathname, uint64_t flags,
                                 uint64_t mode) {
    struct nas_fs_path_request request;
    int rc=nas_fs_user_path_begin((uint64_t)-100,pathname,&request);
    if(rc)return (uint64_t)(int64_t)rc;
    uint64_t result=open_path_owned(request.pathname,&request,flags,mode);
    result=maybe_open_bootstrap(&request,(uint64_t)-100,pathname,flags,mode,result);
    nas_fs_path_end(&request);
    return result;
}

/* NaOS-aligned idea: bind ops once to the shared open description; do not
 * repeat a dispatch by FD kind in every userspace read/write/poll/ioctl. */
static uint64_t vread_null(uint64_t fd,uint64_t addr,uint64_t n) {
    (void)fd;(void)addr;(void)n;return 0;
}
static uint64_t vread_zero(uint64_t fd,uint64_t addr,uint64_t n) {
    (void)fd;
    if(addr>=USER_LIMIT || n>USER_LIMIT-addr)return (uint64_t)-EFAULT;
    uint8_t zero=0;
    for(uint64_t i=0;i<n;i++)
        if(!nas_user_write_bytes(addr+i,&zero,1))
            return i ? i : (uint64_t)-EFAULT;
    return n;
}
static uint64_t vread_dir(uint64_t fd,uint64_t addr,uint64_t n) {
    (void)fd;(void)addr;(void)n;return (uint64_t)-EISDIR;
}
static uint64_t vread_limine(uint64_t fd,uint64_t addr,uint64_t n) {
    const struct limine_file *file=nas_open_files[TASK.fds->entries[fd].slot].boot_file;
    if(!file || !file->address)return (uint64_t)-EBADF;
    uint64_t start=nas_open_files[TASK.fds->entries[fd].slot].position;
    if(start>file->size)return 0;
    if(n>file->size-start)n=file->size-start;
    if(!nas_user_write_bytes(addr,(const uint8_t*)file->address+start,n))
        return (uint64_t)-EFAULT;
    nas_open_files[TASK.fds->entries[fd].slot].position+=n;
    return n;
}
static uint64_t vread_tty(uint64_t fd,uint64_t addr,uint64_t n) {
    return nas_tty_read(addr,n,(nas_open_files[TASK.fds->entries[fd].slot].status_flags&0x800ULL)!=0);
}
static uint64_t vwrite_discard(uint64_t fd,uint64_t addr,uint64_t n) {
    (void)fd;(void)addr;return n;
}
static uint64_t vwrite_tty(uint64_t fd,uint64_t addr,uint64_t n) {
    for(uint64_t i=0;i<n;i++) {
        uint8_t *p=nas_mmu_user_byte(addr+i);
        if(!p)return i ? i : (uint64_t)-EFAULT;
        nas_tty_write_char(*p);
        if(fd==1 && (running_bash || running_busybox))
            observe_stdout((char)*p);
    }
    return n;
}
static uint16_t dpoll_always(unsigned slot,uint16_t events) {
    (void)slot;return events & 0x0005U;
}
static uint16_t vpoll_always(uint64_t fd,uint16_t events) {
    (void)fd;return dpoll_always(0,events);
}
static uint16_t dpoll_tty(unsigned slot,uint16_t events) {
    (void)slot;
    uint16_t out=events & 0x0004U;
    if((events & 0x0001U) && nas_tty_poll_readable())out|=0x0001U;
    return out;
}
static uint16_t vpoll_tty(uint64_t fd,uint16_t events) {
    (void)fd;return dpoll_tty(0,events);
}
static uint16_t dpoll_pipe_read(unsigned slot,uint16_t events) {
    if(nas_pipe_poll_readable(slot))return events & 0x0001U;
    /* A disappeared backend must wake blocked readers to report EBADF.
     * A normally closed writer is already readable as EOF above. */
    return nas_pipe_read_ready(slot)?0x0018U:0;
}
static uint16_t vpoll_pipe_read(uint64_t fd,uint16_t events) {
    return dpoll_pipe_read(TASK.fds->entries[fd].slot,events);
}
static uint16_t dpoll_pipe_write(unsigned slot,uint16_t events) {
    if(nas_pipe_poll_writable(slot))return events & 0x0004U;
    /* A full pipe must remain blocked, but no readers means EPIPE and
     * a blocked writer MUST wake, regardless of requested POLLOUT. */
    return nas_pipe_write_ready(slot)?0x0018U:0;
}
static uint16_t vpoll_pipe_write(uint64_t fd,uint16_t events) {
    return dpoll_pipe_write(TASK.fds->entries[fd].slot,events);
}
static uint64_t vioctl_tty(uint64_t fd,uint64_t cmd,uint64_t arg) {
    (void)fd;
    return nas_tty_ioctl(cmd,arg);
}
static uint64_t vioctl_uart(uint64_t fd,uint64_t cmd,uint64_t arg) {
    (void)fd;
    return (cmd==0x540eULL || nas_tty_enabled())
        ? nas_tty_ioctl(cmd,arg) : (uint64_t)-ENOTTY;
}
static void vrelease_pipe(unsigned slot){nas_pipe_release_description(slot);}
static void vrelease_socket(unsigned slot){
    nas_unix_socket_release(slot);
    nas_pipe_release_description(slot);
}
static const struct nas_file_ops ops_uart={
    .read=vread_tty,.write=vwrite_tty,.poll=vpoll_tty,.poll_description=dpoll_tty,
    .ioctl=vioctl_uart
};
static const struct nas_file_ops ops_tty={
    .read=vread_tty,.write=vwrite_tty,.poll=vpoll_tty,.poll_description=dpoll_tty,
    .ioctl=vioctl_tty
};
static const struct nas_file_ops ops_null={
    .read=vread_null,.write=vwrite_discard,.poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_zero={
    .read=vread_zero,.write=vwrite_discard,.poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_bootfs={.read=vread_limine,.poll=vpoll_always,.poll_description=dpoll_always};
/* Every mounted regular file/dir reuses the SAME read/write/poll File ops.
 * Filesystem types are selected only by the inode's provider callbacks. */
static const struct nas_file_ops ops_vfs_regular={
    .read=vread_regular,.write=vwrite_regular,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_vfs_dir={
    .read=vread_dir,.poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_pipe_read={
    .read=pipe_fd_read,.poll=vpoll_pipe_read,.poll_description=dpoll_pipe_read,
    .release=vrelease_pipe
};
static const struct nas_file_ops ops_pipe_write={
    .write=pipe_fd_write,.poll=vpoll_pipe_write,.poll_description=dpoll_pipe_write,
    .release=vrelease_pipe
};
static uint16_t dpoll_socket(unsigned slot,uint16_t events) {
    if(nas_unix_socket_listening(slot))
        return nas_unix_socket_readable(slot) ? (events&1U) : 0;
    return dpoll_pipe_read(slot,events) | dpoll_pipe_write(slot,events);
}
static uint16_t vpoll_socket(uint64_t fd,uint16_t events) {
    return dpoll_socket(TASK.fds->entries[fd].slot,events);
}
static const struct nas_file_ops ops_socket={
    .read=pipe_fd_read,.write=pipe_fd_write,.poll=vpoll_socket,.poll_description=dpoll_socket,
    .release=vrelease_socket
};
static uint16_t dpoll_pty(unsigned slot,uint16_t events) {
    return nas_pty_poll(slot,events);
}
static uint16_t vpoll_pty(uint64_t fd,uint16_t events) {
    return dpoll_pty(TASK.fds->entries[fd].slot,events);
}
static void vrelease_pty(unsigned slot){nas_pty_release(slot);}
static const struct nas_file_ops ops_pty={
    .read=nas_pty_read,.write=nas_pty_write,.poll=vpoll_pty,.poll_description=dpoll_pty,
    .ioctl=nas_pty_ioctl,.release=vrelease_pty
};
static uint16_t vpoll_epoll(uint64_t fd,uint16_t events){
    return nas_epoll_fd_poll(fd,events);
}
static void vrelease_epoll(unsigned slot){nas_epoll_release(slot);}
static void vrelease_eventfd(unsigned slot){nas_eventfd_release(slot);}
static const struct nas_file_ops ops_epoll={
    .poll=vpoll_epoll,.poll_description=nas_epoll_description_poll,.release=vrelease_epoll
};
static const struct nas_file_ops ops_eventfd={
    .read=nas_eventfd_read,.write=nas_eventfd_write,
    .poll=nas_eventfd_poll,.poll_description=nas_eventfd_poll_slot,.release=vrelease_eventfd
};
static const struct nas_file_ops *const ops_by_kind[]={
    [FD_UART]=&ops_uart,[FD_TTY]=&ops_tty,[FD_NULL]=&ops_null,
    [FD_ZERO]=&ops_zero,[FD_BOOTFS]=&ops_bootfs,
    [FD_ARCHIVE]=&ops_vfs_regular,[FD_DIR]=&ops_vfs_dir,
    [FD_PIPE_R]=&ops_pipe_read,[FD_PIPE_W]=&ops_pipe_write,
    [FD_TMP]=&ops_vfs_regular,[FD_TMP_DIR]=&ops_vfs_dir,
    [FD_EPOLL]=&ops_epoll,
    [FD_EVENTFD]=&ops_eventfd,[FD_SOCKET]=&ops_socket,
    [FD_PTY_MASTER]=&ops_pty,[FD_PTY_SLAVE]=&ops_pty,
    [FD_PROC_FILE]=&ops_vfs_regular,[FD_PROC_DIR]=&ops_vfs_dir
};
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    return kind < sizeof(ops_by_kind)/sizeof(ops_by_kind[0])
           ? ops_by_kind[kind] : 0;
}

/* generic descriptor I/O lives in file_io.c */
/* poll/select and interests live in poll_syscall.c */
/* CWD methods live in namespace_syscall.c */
/* metadata and ownership policies live in metadata_syscall.c */
static uint64_t sys_openat_boot(uint64_t dirfd, uint64_t pathname,
                                uint64_t flags,uint64_t mode) {
    struct nas_fs_path_request request;
    int rc=nas_fs_user_path_begin(dirfd,pathname,&request);
    if(rc)return (uint64_t)(int64_t)rc;
    uint64_t result=open_path_owned(request.pathname,&request,flags,mode);
    result=maybe_open_bootstrap(&request,dirfd,pathname,flags,mode,result);
    nas_fs_path_end(&request);
    return result;
}
/* directory enumeration/stat lookup live in metadata_syscall.c */
/* A known filesystem operation returns its genuine Linux ABI result.
 * Non-FS operations remain owned by the common dispatcher. */
#define FS_RESULT(expr) return (struct nas_fs_result){ .handled = true, .value = (expr) }
struct nas_fs_result nas_fs_syscall(enum nas_syscall_op op,
                                     const uint64_t *args) {
    struct nas_fs_result ns = nas_fs_namespace_syscall(op, args);
    if (ns.handled) return ns;
    struct nas_fs_result socket = nas_fs_socket_syscall(op, args);
    if (socket.handled) return socket;
    struct nas_fs_result metadata = nas_fs_metadata_syscall(op, args);
    if (metadata.handled) return metadata;
    switch (op) {
    case NAS_SC_open: FS_RESULT(sys_open_device(args[1],args[2],args[3]));
    case NAS_SC_openat: FS_RESULT(sys_openat_boot(args[1],args[2],args[3],args[4]));
    case NAS_SC_pread64: FS_RESULT(nas_fs_fd_pread(args[1],args[2],args[3],args[4]));
    case NAS_SC_pwrite64: FS_RESULT(nas_fs_fd_pwrite(args[1],args[2],args[3],args[4]));
    case NAS_SC_pipe: FS_RESULT(sys_pipe2(args[1], 0));
    case NAS_SC_pipe2: FS_RESULT(sys_pipe2(args[1], args[2]));
    case NAS_SC_socketpair: FS_RESULT(sys_socketpair(args[1],args[2],args[3],args[4]));
    case NAS_SC_fcntl: FS_RESULT(sys_fcntl(args[1], args[2], args[3]));
    case NAS_SC_flock: {
        if(args[1]>=NAS_FD_LIMIT ||
           TASK.fds->entries[args[1]].kind==FD_CLOSED)
            FS_RESULT((uint64_t)-EBADF);
        struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[args[1]].slot];
        if(!opened->vnode_pinned ||
           (opened->status_flags&NAS_LINUX_O_PATH))
            FS_RESULT((uint64_t)-EBADF);
        FS_RESULT((uint64_t)(int64_t)nas_vfs_flock(&opened->vnode,
                                                     opened,(unsigned)args[2]));
    }
    case NAS_SC_eventfd:
        FS_RESULT(nas_eventfd_create(args[1],0));
    case NAS_SC_eventfd2:
        FS_RESULT(nas_eventfd_create(args[1],args[2]));
    case NAS_SC_epoll_create1:
        FS_RESULT(nas_epoll_create1(args[1]));
    case NAS_SC_epoll_create:
        FS_RESULT(args[1] ? nas_epoll_create1(0) : (uint64_t)-EINVAL);
    case NAS_SC_epoll_ctl:
        FS_RESULT(nas_epoll_ctl(args[1],args[2],args[3],args[4]));
    case NAS_SC_poll: FS_RESULT(nas_fs_poll(args[1], args[2], args[3]));
    case NAS_SC_ppoll: FS_RESULT(nas_fs_ppoll(args[1], args[2], args[3], args[4]));
    case NAS_SC_dup: FS_RESULT(fd_duplicate(args[1], 0, false));
    case NAS_SC_dup2: FS_RESULT(fd_clone(args[1], args[2], false));
    case NAS_SC_close:
        if (args[1] >= 64 || TASK.fds->entries[args[1]].kind == FD_CLOSED)
            FS_RESULT((uint64_t)-EBADF);
        nas_fs_close_fd(args[1]);
        FS_RESULT(0);
    case NAS_SC_ioctl:
        FS_RESULT(sys_ioctl_fd(args[1],args[2],args[3]));
    case NAS_SC_dup3:
        if (args[1] == args[2] || (args[3] & ~0x80000ULL))
            FS_RESULT((uint64_t)-EINVAL);
        FS_RESULT(fd_clone(args[1], args[2], (args[3] & 0x80000ULL) != 0));
    default:
        return (struct nas_fs_result){ .handled = false, .value = 0 };
    }
}
#undef FS_RESULT
