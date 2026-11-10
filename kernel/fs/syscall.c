#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../syscall/ops.h"
#include "../task.h"
#include "../task/signal.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../pipe.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../mm/frame.h"
#include "../arch/arch.h"
#include "../arch/abi.h"
#include "../boot_image.h"
#include "../random.h"
#include "../arch/linux_stat_abi.h"
#include "../arch/linux_fcntl_abi.h"
#include "../arch/mmu.h"
#include "../drivers/tty.h"
#include "../drivers/vt.h"
#include "../drivers/pty.h"
#include "../drivers/display/fbdev.h"
#include "../drivers/core/chrdev.h"
#if defined(NAS_ENABLE_VIRTIO_INPUT) || \
    defined(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) || \
    defined(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) || \
    defined(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND)
#include "../drivers/input/core.h"
#include "input_chrdev.h"
#endif
#ifdef NAS_ENABLE_H15_QEMU_USB_SERIAL_USERLAND
#include "usb_serial_chrdev.h"
#endif
#include "io.h"
#include "mode.h"
#include "syscall.h"
#include "procfs.h"
#include "file_ops.h"
#include "posix_locks.h"
#include "epoll.h"
#include "eventfd.h"
#include "timerfd.h"
#include "inotify.h"
#include "pidfd.h"
#include "unix_socket.h"
#include "packet_socket.h"
#include "../../ports/lwip/socket_port.h"
#include "../../ports/lwip/network_boot.h"
#include "namespace_syscall.h"
#include "socket_syscall.h"
#include "metadata_syscall.h"
#include "open_path.h"
#include "path_request.h"
#include "lookup.h"
#include "dac.h"
#include "vfs.h"
#include "../time/clock.h"
/* Linux x86-64 filesystem syscall adapter. Backend state is already
 * task-local or refcounted via fd.c, pipe.c, bootfs.c and tmpfs.c.
 * The user ABI stat/dirent encodings are still x86-specific in v1. */
#define TASK (*nas_current_task)
#define current_workdir TASK.fs->cwd
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_putchar(char);
static void nas_fs_fail(const char *msg) __attribute__((noreturn));
static void nas_fs_fail(const char *msg) {
    nas_print("[FAIL] M1 "); nas_print(msg); nas_print("\n");
    nas_arch_halt();
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
    /* Resolve writable lazy/COW destinations before allocating any open
     * descriptions: a user-supplied read-only page is EFAULT, not panic. */
    if (!nas_user_prepare_write(user_pair, 8)) return (uint64_t)-EFAULT;
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
    if (!nas_user_write_bytes(user_pair, output, sizeof(output))) {
        /* A future preemptive kernel may invalidate the preflight. Never
         * retain published descriptors after a failed userspace copy. */
        nas_fs_close_fd(fds[0]);
        nas_fs_close_fd(fds[1]);
        return (uint64_t)-EFAULT;
    }
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
/* A single plan precedes the entire logical transfer, independent of the
 * usercopy chunk boundaries. This preserves PIPE_BUF atomic admission. */
static uint64_t pipe_read_plan(unsigned slot,uint64_t requested) {
    return (uint64_t)nas_pipe_read_amount(
        slot,requested,!!(nas_open_files[slot].status_flags&0x800ULL));
}
static uint64_t pipe_write_plan(unsigned slot,uint64_t requested) {
    return (uint64_t)nas_pipe_write_amount(
        slot,requested,!!(nas_open_files[slot].status_flags&0x800ULL));
}
static uint64_t socket_write_plan(unsigned slot,uint64_t requested) {
    if(nas_pipe_socket_write_shutdown(slot))return (uint64_t)-EPIPE;
    return (uint64_t)nas_socket_write_amount(
        slot,requested,!!(nas_open_files[slot].status_flags&0x800ULL));
}
static uint64_t socket_read_plan(unsigned slot,uint64_t requested) {
    if(nas_pipe_socket_read_shutdown(slot))return 0;
    return pipe_read_plan(slot,requested);
}
/* Providers consume kernel memory only. The File data plane owns every
 * usercopy preflight, staging buffer and partial-fault decision. */
static uint64_t pipe_fd_read(unsigned slot,uint8_t *dst,uint64_t n) {
    for(uint64_t i=0;i<n;i++)dst[i]=nas_pipe_read_byte(slot,(unsigned)i);
    nas_pipe_read_commit(slot,(unsigned)n);
    return n;
}
static uint64_t pipe_fd_write(unsigned slot,const uint8_t *src,uint64_t n) {
    for(uint64_t i=0;i<n;i++)nas_pipe_write_byte(slot,(unsigned)i,src[i]);
    nas_pipe_write_commit(slot,(unsigned)n);
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
    if(nas_open_files[slot].vnode_pinned)
        nas_vfs_notify_file_close(&nas_open_files[slot].vnode,
                                  (nas_open_files[slot].status_flags&3U)!=0U);
    /* flock belongs to the SHARED File, not the current task's FD.
     * dup/fork/SCM_RIGHTS release this lock only on final close. */
    nas_vfs_release_flock(&nas_open_files[slot]);
    if(nas_open_files[slot].vpath.dentry &&
       nas_vfs_path_put(&nas_open_files[slot].vpath))
        nas_fs_fail("File dentry pin corrupted at final close");
    if(nas_open_files[slot].vnode_pinned &&
       nas_vfs_node_unpin(&nas_open_files[slot].vnode))
        nas_fs_fail("File mount pin corrupted at final close");
    if(nas_open_files[slot].chrdev_node)
        nas_chrdev_node_put(nas_open_files[slot].chrdev_node);
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
    /* A blocked openat owns this unpublished descriptor index across
     * CLONE_FILES siblings. dup2/dup3 must not steal its slot while the
     * filesystem worker performs O_CREAT/O_TRUNC side effects. */
    if(!nas_fd_available(nas_current_task,(unsigned)dst) &&
       TASK.fds->entries[dst].kind==FD_CLOSED)
        return (uint64_t)-EBUSY;
    /* Acquire ownership BEFORE closing dst: final_close can reclaim the
     * source backend when dst aliases src, and a saturated File must leave
     * the old destination untouched. attach_owned consumes this one pin. */
    unsigned slot=TASK.fds->entries[src].slot;
    uint8_t kind=TASK.fds->entries[src].kind;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
       nas_open_files[slot].lease_token)return (uint64_t)-EBADF;
    if(src==dst)return dst;
    if(!nas_fd_hold_description(slot))return (uint64_t)-ENFILE;
    nas_fs_close_fd(dst);
    if(!nas_fd_attach_owned(nas_current_task,(unsigned)dst,slot,kind,cloexec))
        nas_fs_fail("dup2 destination publication violated FD invariant");
    return dst;
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
    case 3: return nas_linux_status_flags(nas_open_files[slot].status_flags); /* F_GETFL */
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
    case 1031: /* F_SETPIPE_SZ: Linux int arg, capacity shared by aliases. */
    case 1032: { /* F_GETPIPE_SZ */
        uint8_t kind=TASK.fds->entries[fd].kind;
        if (kind!=FD_PIPE_R && kind!=FD_PIPE_W && kind!=FD_FIFO_RDWR)
            return (uint64_t)-EBADF;
        return (uint64_t)(command==1031 ?
            nas_pipe_set_capacity(slot,(int32_t)arg) :
            nas_pipe_capacity(slot));
    }
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
    if((cmd==0x8910U || cmd==0x8913U || cmd==0x8915U ||
        cmd==0x8919U || cmd==0x891bU || cmd==0x8921U ||
        cmd==0x8933U || cmd==0x8927U || cmd==0x8946U) &&
       (nas_lwip_socket_is_slot(slot) ||
        nas_packet_socket_is_slot(slot))) { /* NIC-backed Linux ifreq */
        /* Device identity, administrative state and link carrier are
         * distinct from DHCP address assignment. SIOCGIFCONF lists only
         * IPv4 addresses; these ioctls can query an attached interface
         * even while its address is pending or the link is down. Linux
         * ifreq is IFNAMSIZ (16) plus a 24-byte LP64 union. */
        uint8_t request[40];
        if(!nas_user_read_bytes(arg,request,sizeof(request)))
            return (uint64_t)-EFAULT;
        struct nas_lwip_guest_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
        unsigned count=nas_lwip_guest_if_snapshot(interfaces);
        if(!count || count>NAS_LWIP_GUEST_IF_LIMIT)return (uint64_t)-ENODEV;
        unsigned found=count;
        if(cmd==0x8910U) { /* ifr_ifindex -> ifr_name */
            int32_t index=0;
            memcpy(&index,request+16,sizeof(index));
            for(unsigned i=0;i<count;i++)
                if((int)interfaces[i].ifindex==index){found=i;break;}
            if(found==count)return (uint64_t)-ENODEV;
            memcpy(request,interfaces[found].name,16);
        } else { /* ifr_name -> NIC characteristics */
            char name[17];
            memcpy(name,request,16);name[16]=0;
            for(unsigned i=0;i<count;i++)
                if(!strcmp(name,interfaces[i].name)) {found=i;break;}
            if(found==count)return (uint64_t)-ENODEV;
            if(cmd==0x8933U) { /* SIOCGIFINDEX */
                const int32_t index=(int32_t)interfaces[found].ifindex;
                memcpy(request+16,&index,4);
            } else if(cmd==0x8946U) { /* SIOCETHTOOL */
                /* Linux ifreq.ifr_data is a native LP64 pointer. The
                 * driver does not implement ETHTOOL_GET_TS_INFO or other
                 * ethtool hardware features; report EOPNOTSUPP, not
                 * ENOTTY (which libpcap treats as a fatal interface
                 * discovery failure). Validate the real named NIC and
                 * the nested user pointer first. */
                uint64_t data=0;
                uint32_t ethtool_command=0;
                memcpy(&data,request+16,sizeof(data));
                if(!data || !nas_user_read_bytes(data,
                                                (uint8_t *)&ethtool_command,
                                                sizeof(ethtool_command)))
                    return (uint64_t)-EFAULT;
                (void)ethtool_command;
                return (uint64_t)-EOPNOTSUPP;
            } else if(cmd==0x8927U) { /* SIOCGIFHWADDR */
                if(interfaces[found].loopback)return (uint64_t)-ENODEV;
                struct nas_netdev *nic=nas_netdev_get(interfaces[found].name);
                if(!nic)return (uint64_t)-ENODEV;
                uint16_t hardware=1U; /* ARPHRD_ETHER */
                memcpy(request+16,&hardware,2);
                memcpy(request+18,nic->mac,6);
                nas_netdev_put(nic);
            } else if(cmd==0x8915U || cmd==0x8919U || cmd==0x891bU) {
                /* SIOCGIFADDR / SIOCGIFBRDADDR / SIOCGIFNETMASK.
                 * The authoritative DHCP address and netmask come from
                 * the same lwIP interface snapshot. An attached device
                 * with no lease must not be assigned a fabricated IP. */
                const struct nas_lwip_guest_if *iface=&interfaces[found];
                if(!iface->ipv4_host_order || iface->prefix_len>32U ||
                   (cmd!=0x8915U && !iface->prefix_len))
                    return (uint64_t)-EADDRNOTAVAIL;
                if(cmd==0x8919U && iface->loopback)
                    return (uint64_t)-EADDRNOTAVAIL;
                uint32_t addr=iface->ipv4_host_order;
                if(cmd!=0x8915U) {
                    uint32_t mask=iface->prefix_len==32U ? UINT32_MAX :
                                  (UINT32_MAX<<(32U-iface->prefix_len));
                    addr=cmd==0x891bU ? mask:(addr|~mask);
                }
                memset(request+16,0,24);
                request[16]=2; /* sockaddr_in.sin_family = AF_INET */
                request[20]=(uint8_t)(addr>>24);
                request[21]=(uint8_t)(addr>>16);
                request[22]=(uint8_t)(addr>>8);
                request[23]=(uint8_t)addr;
            } else if(cmd==0x8913U) {
                /* IFF_RUNNING is a live carrier/operational assertion,
                 * not a fixed consequence of a named physical device. */
                const uint16_t flags=(interfaces[found].up ? 0x0001U:0U) |
                    (interfaces[found].link_up && interfaces[found].up ?
                     0x0040U:0U) |
                    (interfaces[found].loopback ? 0x0008U : 0x1002U);
                memcpy(request+16,&flags,sizeof(flags));
            } else {
                const uint32_t mtu=interfaces[found].mtu;
                if(!mtu)return (uint64_t)-ENODEV;
                memcpy(request+16,&mtu,sizeof(mtu));
            }
        }
        return nas_user_write_bytes(arg,request,sizeof(request)) ?
               0:(uint64_t)-EFAULT;
    }
    if(cmd==0x8912U && nas_lwip_socket_is_slot(slot)) { /* SIOCGIFCONF */
        /* Linux LP64 ifconf = int32 length, pad32, uint64 buffer.
         * Its traditional fixed-width IPv4 ifreq is 16-byte name plus
         * 16-byte sockaddr plus 8 bytes of union padding (40 total).
         * Only actual IPv4 interfaces are returned; the DHCP snapshot
         * comes from the upstream core, never a guessed NIC address. */
        struct { int32_t length;uint32_t pad;uint64_t buffer; } ifconf;
        _Static_assert(sizeof(ifconf)==16,"Linux LP64 ifconf");
        if(!nas_user_read_bytes(arg,(uint8_t *)&ifconf,sizeof(ifconf)))
            return (uint64_t)-EFAULT;
        if(ifconf.length<0)return (uint64_t)-EINVAL;
        struct nas_lwip_guest_ipv4_if interfaces[NAS_LWIP_GUEST_IF_LIMIT]={0};
        unsigned count=nas_lwip_guest_ipv4_ifconf(interfaces);
        if(count>NAS_LWIP_GUEST_IF_LIMIT)return (uint64_t)-EIO;
        unsigned fitting=ifconf.buffer ? (unsigned)ifconf.length/40U : count;
        if(fitting>count)fitting=count;
        const uint32_t actual=(uint32_t)fitting*40U;
        if(!nas_user_prepare_write(arg,sizeof(ifconf)) ||
           (ifconf.buffer && actual &&
            !nas_user_prepare_write(ifconf.buffer,actual)))
            return (uint64_t)-EFAULT;
        for(unsigned i=0;ifconf.buffer && i<fitting;i++) {
            uint8_t request[40]={0};
            memcpy(request,interfaces[i].name,16);
            request[16]=2; /* sockaddr_in.sin_family = AF_INET */
            const uint32_t ip=interfaces[i].ipv4_host_order;
            request[20]=(uint8_t)(ip>>24);
            request[21]=(uint8_t)(ip>>16);
            request[22]=(uint8_t)(ip>>8);
            request[23]=(uint8_t)ip;
            if(!nas_user_write_bytes(ifconf.buffer+(uint64_t)i*40U,
                                     request,sizeof(request)))
                return (uint64_t)-EFAULT;
        }
        ifconf.length=(int32_t)actual;
        return nas_user_write_bytes(arg,(const uint8_t *)&ifconf,
                                    sizeof(ifconf)) ? 0:(uint64_t)-EFAULT;
    }
    if(cmd==0xc020660bU) { /* Linux FS_IOC_FIEMAP, 32-byte header */
        struct nas_linux_fiemap {
            uint64_t start,length;
            uint32_t flags,mapped,count,reserved;
        } fm;
        struct nas_linux_extent {
            uint64_t logical,physical,length,reserved64[2];
            uint32_t flags,reserved[3];
        };
        _Static_assert(sizeof(struct nas_linux_fiemap)==32,"FIEMAP header");
        _Static_assert(sizeof(struct nas_linux_extent)==56,"FIEMAP extent");
        if(nas_open_files[slot].status_flags&NAS_LINUX_O_PATH)
            return (uint64_t)-EBADF;
        const struct nas_open_file *opened=&nas_open_files[slot];
        if(!opened->vnode_pinned)return (uint64_t)-ENOTTY;
        if(!nas_user_read_bytes(arg,(uint8_t *)&fm,sizeof(fm)))
            return (uint64_t)-EFAULT;
        if(fm.flags&~1U)return (uint64_t)-EBADR; /* only FIEMAP_FLAG_SYNC */
        if(!fm.length)return (uint64_t)-EINVAL;
        uint64_t size=nas_vfs_node_size(&opened->vnode);
        uint64_t end=fm.length>UINT64_MAX-fm.start ? UINT64_MAX :
                     fm.start+fm.length;
        if(end>size)end=size;
        uint64_t cursor=fm.start;
        uint32_t mapped=0;
        while(cursor<end && (!fm.count || mapped<fm.count)) {
            uint64_t logical=0,length=0;
            bool last=false;
            int rc=nas_vfs_fiemap_next(&opened->vnode,cursor,end,
                                       &logical,&length,&last);
            if(rc<0)return (uint64_t)(int64_t)rc;
            if(!rc)break;
            if(!length || logical>UINT64_MAX-length ||
               logical+length<=cursor)return (uint64_t)-EIO;
            if(fm.count) {
                /* tmpfs has real allocated pages, but NO disk sector number.
                 * UNKNOWN (not DELALLOC) makes filefrag report the truth. */
                struct nas_linux_extent extent={
                    .logical=logical,.length=length,
                    .flags=2U|(last?1U:0U) /* UNKNOWN | LAST */
                };
                uint64_t displacement=32ULL+(uint64_t)mapped*sizeof(extent);
                if(arg>UINT64_MAX-displacement ||
                   !nas_user_write_bytes(arg+displacement,
                                         (const uint8_t *)&extent,sizeof(extent)))
                    return (uint64_t)-EFAULT;
            }
            mapped++;
            cursor=logical+length;
        }
        fm.mapped=mapped;
        return nas_user_write_bytes(arg,(const uint8_t *)&fm,sizeof(fm))
            ? 0:(uint64_t)-EFAULT;
    }
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
    if(cmd==0x5441ULL) { /* TIOCGPTPEER: open the slave of this PTY master */
        /* Unlike ordinary PTY ioctls this creates a NEW descriptor, so its
         * allocation belongs at the common FD boundary, not in pty.c.
         * util-linux script/openpty uses this Linux interface instead of
         * constructing /dev/pts/N and opening it a second time. */
        if(TASK.fds->entries[fd].kind!=FD_PTY_MASTER)
            return (uint64_t)-ENOTTY;
        uint64_t flags=0;
        if(!nas_linux_open_flags(arg,&flags) ||
           (flags&3ULL)==3ULL ||
           (flags&~(3ULL|0x100ULL|0x800ULL|0x80000ULL)))
            return (uint64_t)-EINVAL;
        const unsigned number=nas_pty_number(slot);
        if(number>=NAS_PTY_LIMIT)return (uint64_t)-EBADF;
        unsigned fds[2],slots[2];
        int capacity=nas_fd_reserve(nas_current_task,0,1,fds,slots,
                                    "TIOCGPTPEER");
        if(capacity)return (uint64_t)(int64_t)capacity;
        int rc=nas_pty_attach(number,slots[0]);
        if(rc)return (uint64_t)(int64_t)rc;
        if(!nas_fd_install_new(nas_current_task,fds[0],slots[0],FD_PTY_SLAVE,
                               nas_linux_open_status_flags(flags),
                               !!(flags&0x80000ULL)))
            nas_fs_fail("reserved TIOCGPTPEER FD was occupied");
        return fds[0];
    }
    if(cmd==0x5451ULL || cmd==0x5450ULL) { /* FIOCLEX / FIONCLEX */
        TASK.fds->entries[fd].cloexec=(uint8_t)(cmd==0x5451ULL);
        return 0;
    }
    const struct nas_file_ops *ops=nas_open_files[slot].ops;
    uint64_t result=ops && ops->ioctl ? ops->ioctl(fd,cmd,arg) :
                    (uint64_t)-ENOTTY;
    return result;
}

/* Pathname / dirfd user-address adapter lives in namespace_syscall.c. */
/* Both offset and vnode are owned by the shared open-file description.
 * The provider callback consumes kernel memory, never an FD or user VA. */
static uint64_t vread_regular_plan(unsigned slot,uint64_t requested) {
    struct nas_open_file *opened=&nas_open_files[slot];
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    const struct nas_fs_node *node=&opened->vnode;
    if(node->is_dir)return (uint64_t)-EISDIR;
    if(!node->filesystem || !node->filesystem->read_at)
        return (uint64_t)-ENODEV;
    /* Only size-authoritative providers promise that offset >= inode size
     * is EOF; dynamic virtual files may report st_size=0 yet yield bytes. */
    if(node->filesystem->read_size_is_eof) {
        uint64_t size=nas_vfs_node_size(node);
        if(opened->position>=size)return 0;
        if(requested>size-opened->position)
            requested=size-opened->position;
    }
    return requested;
}
static uint64_t vread_regular(unsigned slot,uint8_t *dst,uint64_t n) {
    struct nas_open_file *opened=&nas_open_files[slot];
    int64_t got=nas_vfs_read_at(&opened->vnode,opened->position,dst,n);
    if(got>0)opened->position+=(uint64_t)got;
    return (uint64_t)got;
}
static uint64_t vwrite_regular_plan(unsigned slot,uint64_t requested) {
    const struct nas_open_file *opened=&nas_open_files[slot];
    if(!opened->vnode_pinned)return (uint64_t)-EBADF;
    if(!nas_vfs_can_write(&opened->vnode))return (uint64_t)-EROFS;
    return requested;
}
static uint64_t vwrite_regular(unsigned slot,const uint8_t *src,uint64_t n) {
    struct nas_open_file *opened=&nas_open_files[slot];
    /* O_APPEND recomputes the live inode size from the VFS lease, not from
     * a stale open-time snapshot. This position is shared across dup/fork. */
    uint64_t size=nas_vfs_node_size(&opened->vnode);
    uint64_t offset=(opened->status_flags & 0x400ULL) ?
                    size : opened->position;
    int64_t written=nas_vfs_write_at(&opened->vnode,offset,src,n);
    if(written>0) {
        if(offset+(uint64_t)written>opened->vnode.size)
            opened->vnode.size=offset+(uint64_t)written;
        opened->position=offset+(uint64_t)written;
    }
    return (uint64_t)written;
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
    const uint64_t permitted=NAS_LINUX_O_PATH | 3ULL | 0x800ULL | 0x1000ULL | 0x8000ULL |
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
    int pty_number=-1;
    const struct limine_file *real_file=NULL;
    const struct nas_chrdev_provider *chrdev_provider=NULL;
    struct nas_chrdev_node *chrdev_node=NULL;
    unsigned chrdev_unit=0;
    bool have_node=false;
    struct nas_fs_node opened_node={0};
    struct nas_vfs_path directory_path={0};
    char canonical[256]={0};

    /* Reserve both resources without publication BEFORE namei may create
     * or truncate a filesystem inode. EMFILE/ENFILE must never cause
     * O_CREAT or O_TRUNC side effects. This probe is non-mutating. */
    unsigned fds[2],slots[2];
    int capacity=nas_fd_reserve(nas_current_task,0,1,fds,slots,"openat");
    if(capacity)return (uint64_t)(int64_t)capacity;
    const unsigned fd=fds[0],slot=slots[0];

    struct nas_fs_bootstrap_info bootstrap={0};
    bool bootstrap_ready=false;
    if(create) {
        /* O_CREAT must not first manufacture /dev/null as a regular file
         * before discovering the rootless synthetic endpoint. Resolve the
         * literal whitelist from the SAME owned request, then run a
         * side-effect-free nofollow namei. An existing real inode (also a
         * symlink) proceeds through ordinary VFS open, never this bridge. */
        char candidate[256];
        const char *literal=NULL;
        if(request && nas_fs_bootstrap_candidate(request,candidate))
            literal=candidate;
        else if(!request && nas_fs_bootstrap_endpoint(path))literal=path;
        if(literal) {
            int checked=nas_fs_bootstrap_nofollow_probe(request,path);
            if(checked==-ENOENT) {
                if(!nas_fs_bootstrap_describe(literal,&bootstrap))
                    bootstrap_ready=true;
            } else if(checked)return (uint64_t)(int64_t)checked;
        }
    }
    if(!bootstrap_ready) {
        /* The VFS owns namei, O_CREAT, inode mode/permission validation,
         * truncate and the returned mount/inode pin as ONE transaction.
         * Linux native flags are decoded here and never enter providers. */
        const struct nas_vfs_open_options options={
            .follow_final=follow_final,.create=create,
            .exclusive=exclusive,.truncate=truncate,.path_only=path_only,
            .directory_only=(flags&0x10000ULL)!=0,.access=access,
            .create_mode=nas_fs_create_mode(create_mode,TASK.fs->umask)
        };
        int rc=nas_vfs_open_path_at(
            request && request->start.dentry ? &request->start : NULL,
            request && request->root.dentry ? &request->root : NULL,
            path,&options,&opened_node,&directory_path);
        if(!rc) {
            have_node=true;
            kind=opened_node.is_dir?FD_DIR:FD_ARCHIVE;
            if((opened_node.mode&0170000U)==0020000U) {
                /* VFS is the namespace authority; the class registry is
                 * ONLY the rdev -> live FileOps/backend owner. Resolve
                 * before publishing any process FD, while the real inode
                 * and the namespace Dentry remain independently pinned. */
                struct nas_chrdev_info info={0};
                rc=nas_chrdev_resolve_rdev(opened_node.rdev,&info,
                                          &chrdev_provider,&chrdev_node);
                if(rc) {
                    (void)nas_vfs_path_put(&directory_path);
                    (void)nas_vfs_node_unpin(&opened_node);
                    return (uint64_t)(int64_t)rc;
                }
                kind=info.fd_kind;
                chrdev_unit=info.unit;
                if(kind==FD_PTY_SLAVE)pty_number=(int)info.unit;
            }
            if(!path_only && (opened_node.mode&0170000U)==0010000U)
                kind=access==0U ? FD_PIPE_R :
                     access==1U ? FD_PIPE_W : FD_FIFO_RDWR;
            /* origin_path is a diagnostic hint, never a second namei or
             * part of the successful O_CREAT/O_TRUNC transaction. A long
             * Path must not turn a committed open into an error because
             * this optional legacy 256-byte snapshot cannot represent it. */
            if(directory_path.dentry)
                (void)nas_vfs_path_string(&directory_path,canonical);
        } else if(rc==-ENOENT) {
            /* Rootless bootstrap is the exceptional result of THIS open
             * transaction. Keep the owned root/dirfd anchors through its
             * nofollow probe; never reconstruct a name and reopen it via
             * the process root after the first namei already failed. */
            char candidate[256];
            const char *literal=NULL;
            if(request && nas_fs_bootstrap_candidate(request,candidate))
                literal=candidate;
            else if(!request && nas_fs_bootstrap_endpoint(path))literal=path;
            if(!literal ||
               nas_fs_bootstrap_nofollow_probe(request,path)!=-ENOENT ||
               nas_fs_bootstrap_describe(literal,&bootstrap))
                return (uint64_t)(int64_t)rc;
            bootstrap_ready=true;
        } else return (uint64_t)(int64_t)rc;
    }
    /* No inode ownership/search authority exists for the rootless
     * synthetic bootstrap bridge: never grant it to nonroot callers. */
    if(bootstrap_ready && nas_dac_nonroot())return (uint64_t)-EACCES;
    if(bootstrap_ready) {
        kind=bootstrap.fd_kind;
        pty_number=bootstrap.pty_number;
        real_file=bootstrap.boot_file;
        chrdev_provider=bootstrap.chrdev_provider;
        chrdev_node=bootstrap.chrdev_node;
        chrdev_unit=bootstrap.chrdev_unit;
    }
    if(!path_only && kind==FD_TTY &&
       (have_node ? opened_node.rdev : bootstrap.rdev)==(5U<<8)) {
        if(!nas_tty_controlling() && !nas_pty_controlling()) {
            if(have_node) {
                (void)nas_vfs_path_put(&directory_path);
                (void)nas_vfs_node_unpin(&opened_node);
            }
            return (uint64_t)-ENXIO;
        }
        if(nas_pty_controlling())kind=FD_PTY_SLAVE;
    }

    if(!have_node) {
        if(flags&0x10000ULL)return (uint64_t)-ENOTDIR;
        if(create && exclusive)return (uint64_t)-EEXIST;
        if(kind==FD_BOOTFS) {
            if(flags&(0x40ULL|0x400ULL))return (uint64_t)-EROFS;
            if(access)return (uint64_t)-EACCES;
        }
    }

    /* Pin the published class node exactly once, then attach provider
     * state to the still-unpublished shared OpenFile slot. Both are rolled
     * back on failure, before a process FD becomes visible. */
    int open_error=chrdev_node ? nas_chrdev_node_pin(chrdev_node) : 0;
    if(open_error)goto open_rollback;
    if(!path_only && have_node &&
       (opened_node.mode&0170000U)==0010000U) {
        open_error=nas_pipe_named_attach(opened_node.mount_id,
                      opened_node.ino,slot,(unsigned)access,
                      (flags&0x800ULL)!=0);
        if(open_error)goto open_rollback;
    }
    if(!path_only && kind==FD_PTY_MASTER) {
        open_error=nas_pty_new(slot);
    } else if(!path_only && kind==FD_PTY_SLAVE) {
        open_error=pty_number>=0 ?
            nas_pty_attach((unsigned)pty_number,slot) :
            nas_pty_attach_controlling(slot);
    }
    if(!open_error && chrdev_provider && chrdev_provider->open && !path_only) {
        /* A character device owns one consumer per shared File, not one
         * per FD alias. Do not publish a descriptor until it is attached. */
        open_error=chrdev_provider->open(slot,chrdev_unit);
    }
    if(open_error) {
        if(chrdev_node)nas_chrdev_node_put(chrdev_node);
        goto open_rollback;
    }
    /* Inode AND optional directory Dentry are one VFS-owned open
     * transaction. The syscall layer just transfers their references
     * to this shared File description; it never constructs a Path. */
    if(!nas_fd_install_new(nas_current_task,fd,slot,kind,
                           nas_linux_open_status_flags(flags),
                           !!(flags&0x80000ULL)))
        nas_fs_fail("reserved openat description was occupied");
    /* POSIX/Linux: a session leader opening a real VT without O_NOCTTY
     * acquires its controlling terminal, independent of serial OpenRC. */
    if(!path_only && kind==FD_VT)nas_vt_open_session(flags);
    /* Likewise posix_openpt/grantpt/unlockpt + setsid + open(pts) must
     * acquire the PTY as controlling tty without an explicit TIOCSCTTY.
     * This is the actual sshpass login sequence, not a synthetic shim. */
    if(!path_only && kind==FD_PTY_SLAVE)
        nas_pty_open_session(slot,flags);
    if(have_node) {
        nas_open_files[slot].vnode=opened_node;
        nas_open_files[slot].vnode_pinned=true;
        nas_open_files[slot].vpath=directory_path;
        nas_open_files[slot].chrdev_node=chrdev_node;
    }
    if(bootstrap_ready) {
        nas_open_files[slot].chrdev_node=chrdev_node;
        nas_open_files[slot].boot_identity.dev=bootstrap.dev;
        nas_open_files[slot].boot_identity.ino=bootstrap.ino;
        nas_open_files[slot].boot_identity.rdev=bootstrap.rdev;
        nas_open_files[slot].boot_identity.mode=bootstrap.mode;
        nas_open_files[slot].boot_identity.size=bootstrap.size;
        nas_open_files[slot].boot_identity.valid=true;
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
open_rollback:
    if(have_node) {
        if(directory_path.dentry)(void)nas_vfs_path_put(&directory_path);
        (void)nas_vfs_node_unpin(&opened_node);
    }
    return (uint64_t)(int64_t)open_error;
}
uint64_t nas_fs_open_path(const char *path,uint64_t native_flags,
                          uint64_t create_mode) {
    return open_path_owned(path,NULL,native_flags,create_mode);
}
/* Anonymous regular files use the SAME VFS inode lease, FileOps and FD
 * namespace as a named open. The VFS creates an initially unlinked tmpfs
 * inode and transfers its single owned mount/inode pin to this OpenFile.
 * No pathname creation, umask or /tmp directory permissions participate.
 * The visible memfd name is diagnostics only, never an inode lookup key. */
uint64_t nas_fs_open_anon_tmpfs(const char *user_name,bool cloexec) {
    if(!user_name)return (uint64_t)-EINVAL;
    unsigned fds[2],slots[2];
    int rc=nas_fd_reserve(nas_current_task,0,1,fds,slots,"memfd_create");
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_fs_node node={0};
    rc=nas_vfs_create_anonymous_tmpfs(0777U,&node);
    if(rc)return (uint64_t)(int64_t)rc;
    const unsigned fd=fds[0],slot=slots[0];
    if(!nas_fd_install_new(nas_current_task,fd,slot,FD_ARCHIVE,2ULL,cloexec)) {
        if(nas_vfs_node_unpin(&node))nas_fs_fail("memfd failed pin rollback");
        return (uint64_t)-EAGAIN;
    }
    struct nas_open_file *opened=&nas_open_files[slot];
    opened->vnode=node;
    opened->vnode_pinned=true;
    /* Linux presents /memfd:<description> (deleted) in /proc/self/fd.
     * Existing OpenFile diagnostic hints are bounded to 256 bytes; keep
     * the deletion suffix even when the permitted 249-byte name is longer.
     * No operation may resolve or reopen this diagnostic string. */
    static const char prefix[]="/memfd:";
    static const char suffix[]=" (deleted)";
    char *label=opened->origin_path;
    unsigned written=0;
    for(unsigned i=0;i<sizeof(prefix)-1;i++)label[written++]=prefix[i];
    for(unsigned i=0;user_name[i] &&
           written+sizeof(suffix)<sizeof(opened->origin_path);i++)
        label[written++]=user_name[i];
    for(unsigned i=0;i<sizeof(suffix)-1;i++)label[written++]=suffix[i];
    label[written]=0;
    return fd;
}
/* Direct Limine ELF modules have no mounted inode in the rootless boot.
 * Their stable identity comes from the module's own path, not a process FD
 * or a kernel pointer exposed through st_ino. Interpreter path aliases
 * therefore retain the same inode number across separate opens. */
uint64_t nas_fs_bootstrap_module_ino(const struct limine_file *file) {
    if(!file || !file->path)return 0;
    uint64_t hash=14695981039346656037ULL;
    for(const unsigned char *p=(const unsigned char *)file->path;*p;p++)
        hash=(hash^*p)*1099511628211ULL;
    return hash ? hash : 1U;
}
/* The only fallback for genuinely unmounted bootstrap endpoints. Device
 * ownership and /dev publication are now delegated to the character class
 * registry; syscall dispatch never enumerates device names or classes.
 * The caller must establish that Path-first VFS lookup missed, so no
 * synthetic device can hide a real mounted inode. */
int nas_fs_bootstrap_describe(const char *path,
                              struct nas_fs_bootstrap_info *out) {
    if(!out)return -EINVAL;
    *out=(struct nas_fs_bootstrap_info){.pty_number=-1};
    if(!path || path[0]!='/')return -ENOENT;
    struct nas_chrdev_info chrdev={0};
    const struct nas_chrdev_provider *provider=NULL;
    struct nas_chrdev_node *node=NULL;
    int found=nas_chrdev_describe(path,&chrdev,&provider,&node);
    if(!found) {
        out->fd_kind=chrdev.fd_kind;
        out->dev=chrdev.dev;
        out->ino=chrdev.ino;
        out->rdev=chrdev.rdev;
        out->mode=chrdev.mode;
        out->size=chrdev.size;
        out->chrdev_unit=chrdev.unit;
        out->chrdev_provider=provider;
        out->chrdev_node=node;
        if(chrdev.fd_kind==FD_PTY_SLAVE)
            out->pty_number=(int)chrdev.unit;
        return 0;
    }
    if(found!=-ENOENT)return found;
    const struct limine_file *module=nas_boot_limine_module_path(path);
    if(!module)return -ENOENT;
    out->fd_kind=FD_BOOTFS;
    out->boot_file=module;
    out->dev=1;
    out->ino=nas_fs_bootstrap_module_ino(module);
    out->mode=0100555U;
    out->size=module->size;
    return 0;
}
/* This is NOT a second namei for ordinary missing paths. In a direct-ELF
 * rootless boot a few synthetic devices and Limine modules have no VFS
 * inode; only these explicit bootstrap endpoints may use a literal name.
 * In particular ENOENT from a renamed/unlinked directory FD never causes
 * an arbitrary O_CREAT to be retried through a pathname reconstructed from
 * that FD. */
bool nas_fs_bootstrap_endpoint(const char *path) {
    struct nas_fs_bootstrap_info info;
    return nas_fs_bootstrap_describe(path,&info)==0;
}
bool nas_fs_bootstrap_candidate(const struct nas_fs_path_request *request,
                                 char candidate[256]) {
    if(!request || !request->pathname || !request->pathname[0] || !candidate)
        return false;
    if(request->pathname[0]=='/') {
        size_t n=strnlen(request->pathname,256);
        if(n==256)return false;
        memcpy(candidate,request->pathname,n+1);
    } else {
        if(!request->start.dentry)return false;
        char base[256];
        if(nas_vfs_path_string(&request->start,base))return false;
        size_t parent_length=strlen(base);
        size_t length=strnlen(request->pathname,256);
        if(!parent_length || !length || length==256)return false;
        /* Only bootstrap endpoint spelling may be projected: ordinary
         * relative paths always use Path-first namei. Preserve literal
         * components (including '.'/'..') for whitelist rejection. */
        unsigned separator=parent_length>1 && base[parent_length-1]!='/' ? 1U:0U;
        if(parent_length+separator+length>=256)return false;
        memcpy(candidate,base,parent_length);
        if(separator)candidate[parent_length++]='/';
        memcpy(candidate+parent_length,request->pathname,length+1);
    }
    return nas_fs_bootstrap_endpoint(candidate);
}
int nas_fs_bootstrap_nofollow_probe(const struct nas_fs_path_request *request,
                                    const char *path) {
    struct nas_vfs_path existing={0};
    int rc=nas_fs_lookup_path_at(
        request && request->start.dentry ? &request->start : NULL,
        request && request->root.dentry ? &request->root : NULL,
        path,false,&existing);
    if(existing.dentry && nas_vfs_path_put(&existing))
        nas_fs_fail("bootstrap nofollow Path reference corrupted");
    return rc;
}
static uint64_t sys_open_device(uint64_t pathname, uint64_t flags,
                                 uint64_t mode) {
    struct nas_fs_path_request request;
    int rc=nas_fs_user_path_begin((uint64_t)-100,pathname,&request);
    if(rc)return (uint64_t)(int64_t)rc;
    uint64_t result=open_path_owned(request.pathname,&request,flags,mode);
    nas_fs_path_end(&request);
    return result;
}

/* NaOS-aligned idea: bind ops once to the shared open description; do not
 * repeat a dispatch by FD kind in every userspace read/write/poll/ioctl. */
static uint64_t vread_null_plan(unsigned slot,uint64_t requested) {
    (void)slot;(void)requested;
    return 0;
}
static uint64_t vread_null(unsigned slot,uint8_t *dst,uint64_t n) {
    (void)slot;(void)dst;(void)n;
    return 0;
}
static uint64_t vread_zero(unsigned slot,uint8_t *dst,uint64_t n) {
    (void)slot;
    memset(dst,0,(size_t)n);
    return n;
}
/* Both Linux random character endpoints consume the SAME seed-backed
 * ChaCha20 stream as getrandom(2). The boot entropy quality contract is
 * shared; never synthesize constant bytes or pretend an unseeded RNG works.
 * file_io's kernel-buffer provider path makes EFAULT/partial copyout safe. */
static uint64_t vread_random(unsigned slot,uint8_t *dst,uint64_t n) {
    (void)slot;
    return nas_random_bytes(dst,n) ? n : (uint64_t)-ENOSYS;
}
/* Injecting entropy/reseeding via a device write is not supported. A write
 * must not claim successful mixing while silently discarding the input. */
static uint64_t vwrite_random(unsigned slot,const uint8_t *src,uint64_t n) {
    (void)slot;(void)src;(void)n;
    return (uint64_t)-EOPNOTSUPP;
}
static uint64_t vread_dir_plan(unsigned slot,uint64_t requested) {
    (void)slot;(void)requested;
    return (uint64_t)-EISDIR;
}
static uint64_t vread_dir(unsigned slot,uint8_t *dst,uint64_t n) {
    (void)slot;(void)dst;(void)n;
    return (uint64_t)-EISDIR;
}
static uint64_t vread_limine_plan(unsigned slot,uint64_t requested) {
    const struct nas_open_file *opened=&nas_open_files[slot];
    const struct limine_file *file=opened->boot_file;
    if(!file || !file->address)return (uint64_t)-EBADF;
    if(opened->position>=file->size)return 0;
    uint64_t remaining=file->size-opened->position;
    return requested<remaining ? requested : remaining;
}
static uint64_t vread_limine(unsigned slot,uint8_t *dst,uint64_t n) {
    struct nas_open_file *opened=&nas_open_files[slot];
    const struct limine_file *file=opened->boot_file;
    memcpy(dst,(const uint8_t*)file->address+opened->position,(size_t)n);
    opened->position+=n;
    return n;
}
static uint64_t fb_transfer_plan(unsigned slot,uint64_t requested) {
    const uint64_t pos=nas_open_files[slot].position;
    const uint64_t size=nas_fbdev_length();
    if(!size)return (uint64_t)-ENODEV;
    if(pos>=size)return 0;
    const uint64_t remaining=size-pos;
    return requested<remaining ? requested : remaining;
}
static uint64_t fb_read(unsigned slot,uint8_t *dst,uint64_t count) {
    struct nas_open_file *file=&nas_open_files[slot];
    uint64_t got=nas_fbdev_read_at(file->position,dst,count);
    file->position+=got;
    return got;
}
static uint64_t fb_write(unsigned slot,const uint8_t *src,uint64_t count) {
    struct nas_open_file *file=&nas_open_files[slot];
    uint64_t done=nas_fbdev_write_at(file->position,src,count);
    file->position+=done;
    return done;
}
static uint64_t fb_read_at(unsigned slot,uint64_t offset,
                           uint8_t *dst,uint64_t count) {
    (void)slot;
    return nas_fbdev_read_at(offset,dst,count);
}
static uint64_t fb_write_at(unsigned slot,uint64_t offset,
                            const uint8_t *src,uint64_t count) {
    (void)slot;
    return nas_fbdev_write_at(offset,src,count);
}
/* This is the syscall adapter: the hardware driver only emits Linux-shaped
 * kernel objects. Usercopy belongs here, never in the framebuffer driver. */
static uint64_t fb_ioctl(uint64_t fd,uint64_t cmd,uint64_t arg) {
    (void)fd;
    if(cmd==NAS_FBIOGET_VSCREENINFO) {
        struct nas_fb_var_screeninfo info;
        if(!nas_fbdev_var_screeninfo(&info))return (uint64_t)-ENODEV;
        return nas_user_write_bytes(arg,(const uint8_t *)&info,sizeof(info)) ?
               0 : (uint64_t)-EFAULT;
    }
    if(cmd==NAS_FBIOPUT_VSCREENINFO) {
        struct nas_fb_var_screeninfo info;
        if(!nas_user_read_bytes(arg,(uint8_t *)&info,sizeof(info)))
            return (uint64_t)-EFAULT;
        int rc=nas_fbdev_put_var_screeninfo(&info);
        if(rc)return (uint64_t)(int64_t)rc;
        return nas_user_write_bytes(arg,(const uint8_t *)&info,sizeof(info)) ?
               0 : (uint64_t)-EFAULT;
    }
    if(cmd==NAS_FBIOGET_FSCREENINFO) {
        struct nas_fb_fix_screeninfo info;
        if(!nas_fbdev_fix_screeninfo(&info))return (uint64_t)-ENODEV;
        return nas_user_write_bytes(arg,(const uint8_t *)&info,sizeof(info)) ?
               0 : (uint64_t)-EFAULT;
    }
    if(cmd==NAS_FBIOPAN_DISPLAY) {
        struct nas_fb_var_screeninfo info;
        if(!nas_user_read_bytes(arg,(uint8_t *)&info,sizeof(info)))
            return (uint64_t)-EFAULT;
        return (uint64_t)(int64_t)nas_fbdev_pan_display(&info);
    }
    return (uint64_t)-ENOTTY;
}
static uint64_t vread_tty(unsigned slot,uint8_t *dst,uint64_t n) {
    return nas_tty_read(dst,n,(nas_open_files[slot].status_flags&0x800ULL)!=0);
}
static uint64_t vread_tty_plan(unsigned slot,uint64_t requested) {
    return nas_tty_read_plan(
        requested,(nas_open_files[slot].status_flags&0x800ULL)!=0);
}
static uint64_t vwrite_discard(unsigned slot,const uint8_t *src,uint64_t n) {
    (void)slot;(void)src;
    return n;
}
static uint64_t vwrite_tty(unsigned slot,const uint8_t *src,uint64_t n) {
    (void)slot;
    for(uint64_t i=0;i<n;i++)nas_tty_write_char(src[i]);
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
    if(nas_pipe_poll_readable(slot))
        return (events&0x0001U)|(nas_pipe_read_hup(slot)?0x0010U:0);
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
static uint16_t dpoll_fifo_rw(unsigned slot,uint16_t events) {
    return dpoll_pipe_read(slot,events)|dpoll_pipe_write(slot,events);
}
static uint16_t vpoll_fifo_rw(uint64_t fd,uint16_t events) {
    return dpoll_fifo_rw(TASK.fds->entries[fd].slot,events);
}
static uint64_t vioctl_tty(uint64_t fd,uint64_t cmd,uint64_t arg) {
    (void)fd;
    return nas_tty_ioctl(cmd,arg);
}
/* Real framebuffer VT opens are separate Linux Files from boot UART. */
static uint64_t vread_vt(unsigned slot,uint8_t *dst,uint64_t n) {
    return nas_vt_read(dst,n,(nas_open_files[slot].status_flags&0x800ULL)!=0);
}
static uint64_t vread_vt_plan(unsigned slot,uint64_t n) {
    return nas_vt_read_plan(n,(nas_open_files[slot].status_flags&0x800ULL)!=0);
}
static uint64_t vwrite_vt(unsigned slot,const uint8_t *src,uint64_t n) {
    (void)slot;
    return nas_vt_write(src,n);
}
static uint16_t dpoll_vt(unsigned slot,uint16_t events) {
    (void)slot;
    uint16_t ready=events&0x0004U;
    if((events&0x0001U)&&nas_vt_poll_readable())ready|=0x0001U;
    return ready;
}
static uint16_t vpoll_vt(uint64_t fd,uint16_t events) {
    (void)fd;return dpoll_vt(0,events);
}
static void vt_wait_start(unsigned slot,struct nas_task *task,uint64_t n) {
    (void)slot;(void)n;nas_vt_blocking_read_start(task);
}
static bool vt_wait_ready(unsigned slot,struct nas_task *task,uint64_t n) {
    (void)slot;return nas_vt_read_ready(task,n);
}
static uint64_t vioctl_vt(uint64_t fd,uint64_t cmd,uint64_t arg) {
    (void)fd;return nas_vt_ioctl(cmd,arg);
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
    .read_plan=vread_tty_plan,.read=vread_tty,.write=vwrite_tty,
    .aggregate_readv=true,.read_finalize_zero=true,
    .io_wait_flags=NAS_IO_WAIT_TTY,
    .poll=vpoll_tty,.poll_description=dpoll_tty,
    .ioctl=vioctl_uart
};
static const struct nas_file_ops ops_tty={
    .read_plan=vread_tty_plan,.read=vread_tty,.write=vwrite_tty,
    .aggregate_readv=true,.read_finalize_zero=true,
    .io_wait_flags=NAS_IO_WAIT_TTY,
    .poll=vpoll_tty,.poll_description=dpoll_tty,
    .ioctl=vioctl_tty
};
static const struct nas_file_ops ops_vt={
    .read_plan=vread_vt_plan,.read=vread_vt,.write=vwrite_vt,
    .aggregate_readv=true,.read_finalize_zero=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL_EAGAIN|NAS_IO_WAIT_RETRY,
    .read_wait_start=vt_wait_start,.read_wait_ready=vt_wait_ready,
    .poll=vpoll_vt,.poll_description=dpoll_vt,
    .ioctl=vioctl_vt
};
static const struct nas_file_ops ops_null={
    .read_plan=vread_null_plan,.read=vread_null,.write=vwrite_discard,
    .write_ignores_source=true,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_zero={
    .read=vread_zero,.write=vwrite_discard,.write_ignores_source=true,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_random={
    .read=vread_random,.write=vwrite_random,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_bootfs={
    .read_plan=vread_limine_plan,.read=vread_limine,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_fbdev={
    .read_plan=fb_transfer_plan,.write_plan=fb_transfer_plan,
    .read=fb_read,.write=fb_write,
    .read_at=fb_read_at,.write_at=fb_write_at,
    .poll=vpoll_always,.poll_description=dpoll_always,
    .ioctl=fb_ioctl
};
/* Every mounted regular file/dir reuses the SAME read/write/poll File ops.
 * Filesystem types are selected only by the inode's provider callbacks. */
static const struct nas_file_ops ops_vfs_regular={
    .read_plan=vread_regular_plan,.write_plan=vwrite_regular_plan,
    .read=vread_regular,.write=vwrite_regular,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_vfs_dir={
    .read_plan=vread_dir_plan,.read=vread_dir,
    .poll=vpoll_always,.poll_description=dpoll_always
};
static const struct nas_file_ops ops_pipe_read={
    .read_plan=pipe_read_plan,.read=pipe_fd_read,
    .aggregate_readv=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=vpoll_pipe_read,.poll_description=dpoll_pipe_read,
    .release=vrelease_pipe
};
static const struct nas_file_ops ops_pipe_write={
    .write_plan=pipe_write_plan,.write=pipe_fd_write,
    .atomic_write_limit=NAS_PIPE_CAP,.aggregate_writev=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .sends_sigpipe=true,
    .poll=vpoll_pipe_write,.poll_description=dpoll_pipe_write,
    .release=vrelease_pipe
};
static const struct nas_file_ops ops_fifo_rw={
    .read_plan=pipe_read_plan,.read=pipe_fd_read,
    .write_plan=pipe_write_plan,.write=pipe_fd_write,
    .atomic_write_limit=NAS_PIPE_CAP,
    .aggregate_readv=true,.aggregate_writev=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .sends_sigpipe=true,
    .poll=vpoll_fifo_rw,.poll_description=dpoll_fifo_rw,
    .release=vrelease_pipe
};
static uint16_t dpoll_socket(unsigned slot,uint16_t events) {
    if(nas_unix_socket_listening(slot))
        return nas_unix_socket_readable(slot) ? (events&1U) : 0;
    /* EOF/POLLHUP on read side; write direction may still be live. */
    if(nas_pipe_socket_read_shutdown(slot))
        return (events&1U)|0x10U|dpoll_pipe_write(slot,events);
    return dpoll_pipe_read(slot,events) | dpoll_pipe_write(slot,events);
}
static uint16_t vpoll_socket(uint64_t fd,uint16_t events) {
    return dpoll_socket(TASK.fds->entries[fd].slot,events);
}
static const struct nas_file_ops ops_socket={
    .read_plan=socket_read_plan,.write_plan=socket_write_plan,
    .read=pipe_fd_read,.write=pipe_fd_write,
    .aggregate_readv=true,.aggregate_writev=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .sends_sigpipe=true,
    .poll=vpoll_socket,.poll_description=dpoll_socket,
    .release=vrelease_socket
};
static uint16_t dpoll_pty(unsigned slot,uint16_t events) {
    return nas_pty_poll(slot,events);
}
static uint16_t vpoll_pty(uint64_t fd,uint16_t events) {
    return dpoll_pty(TASK.fds->entries[fd].slot,events);
}
static uint64_t vread_pty_plan(unsigned slot,uint64_t requested) {
    return nas_pty_read_plan_mode(slot,requested,
                           (nas_open_files[slot].status_flags&0x800ULL)!=0);
}
static uint64_t vread_pty(unsigned slot,uint8_t *dst,uint64_t count) {
    return nas_pty_read_mode(slot,dst,count,
                           (nas_open_files[slot].status_flags&0x800ULL)!=0);
}
static void vrelease_pty(unsigned slot){nas_pty_release(slot);}
static const struct nas_file_ops ops_pty={
    .read_plan=vread_pty_plan,.read=vread_pty,.write=nas_pty_write,
    .aggregate_readv=true,.read_finalize_zero=true,
    /* An empty blocking PTY read must park until real master input arrives.
     * Unlike pipe/socket initial admission, its normal wait errno is EAGAIN.
     * A nonblocking descriptor still returns EAGAIN to the caller. */
    .io_wait_flags=NAS_IO_WAIT_INITIAL_EAGAIN|NAS_IO_WAIT_RETRY,
    .read_wait_start=nas_pty_read_wait_start,
    .read_wait_ready=nas_pty_read_wait_ready,
    .poll=vpoll_pty,.poll_description=dpoll_pty,
    .ioctl=nas_pty_ioctl,.release=vrelease_pty
};
static uint16_t vpoll_epoll(uint64_t fd,uint16_t events){
    return nas_epoll_fd_poll(fd,events);
}
static void vrelease_epoll(unsigned slot){nas_epoll_release(slot);}
static void vrelease_eventfd(unsigned slot){nas_eventfd_release(slot);}
/* signalfd is a genuine File: its mask belongs to the refcounted open
 * description, while pending signals belong to the active task/TGID. This
 * works across dup/fork without creating an independent fake signal queue. */
static uint64_t signalfd_masks[NAS_OPEN_FILE_LIMIT];
static uint64_t sys_signalfd4(uint64_t raw_fd,uint64_t user_mask,
                             uint64_t mask_size,uint64_t flags) {
    if(mask_size!=8ULL || (flags&~(0x800ULL|0x80000ULL)))
        return (uint64_t)-EINVAL;
    uint64_t mask=0;
    if(!nas_user_read_u64(user_mask,&mask))return (uint64_t)-EFAULT;
    mask&=~((1ULL<<(NAS_SIGKILL-1U))|(1ULL<<(NAS_SIGSTOP-1U)));
    int fd=(int32_t)(uint32_t)raw_fd;
    if(fd!=-1) {
        if(fd<0 || fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind!=FD_SIGNALFD)
            return (uint64_t)-EBADF;
        unsigned slot=TASK.fds->entries[fd].slot;
        signalfd_masks[slot]=mask;
        return (uint64_t)fd;
    }
    unsigned fds[2],slots[2];
    int rc=nas_fd_reserve(nas_current_task,0,1,fds,slots,"signalfd4");
    if(rc)return (uint64_t)(int64_t)rc;
    signalfd_masks[slots[0]]=mask;
    if(!nas_fd_install_new(nas_current_task,fds[0],slots[0],FD_SIGNALFD,
                           flags&0x800ULL,!!(flags&0x80000ULL)))
        nas_fs_fail("reserved signalfd description was occupied");
    return fds[0];
}
static uint64_t signalfd_read_plan(unsigned slot,uint64_t requested) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return (uint64_t)-EBADF;
    if(requested<128ULL)return (uint64_t)-EINVAL;
    if(!nas_signal_signalfd_ready(nas_current_task,signalfd_masks[slot]))
        return (nas_open_files[slot].status_flags&0x800ULL)?
               (uint64_t)-EAGAIN:(uint64_t)-EOPNOTSUPP;
    return 128ULL;
}
static uint64_t signalfd_read(unsigned slot,uint8_t *dst,uint64_t length) {
    uint64_t plan=signalfd_read_plan(slot,length);
    if((int64_t)plan<0)return plan;
    if(!dst)return (uint64_t)-EFAULT;
    struct nas_signalfd_event event={0};
    unsigned signo=nas_signal_signalfd_take(nas_current_task,
                                             signalfd_masks[slot],&event);
    if(!signo)return (uint64_t)-EAGAIN;
    /* Native Linux signalfd_siginfo: signo@0, code@8, pid@12,
     * status@40, all 32-bit fields in the same LP64 ABI on three ISAs. */
    memset(dst,0,128);
    memcpy(dst,&event.signo,4);
    memcpy(dst+8,&event.code,4);
    memcpy(dst+12,&event.pid,4);
    memcpy(dst+40,&event.status,4);
    return 128ULL;
}
uint16_t nas_fs_signalfd_poll_task(unsigned slot,uint16_t events,
                                  const struct nas_task *task) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return 0;
    return (events&1U) &&
        nas_signal_signalfd_ready(task,signalfd_masks[slot])?1U:0U;
}
static uint16_t signalfd_poll_slot(unsigned slot,uint16_t events) {
    return nas_fs_signalfd_poll_task(slot,events,nas_current_task);
}
static uint16_t signalfd_poll_fd(uint64_t fd,uint16_t events) {
    if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind!=FD_SIGNALFD)return 0;
    return signalfd_poll_slot(TASK.fds->entries[fd].slot,events);
}
static void signalfd_release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)signalfd_masks[slot]=0;
}
static const struct nas_file_ops ops_epoll={
    .poll=vpoll_epoll,.poll_description=nas_epoll_description_poll,.release=vrelease_epoll
};
static const struct nas_file_ops ops_eventfd={
    .read_plan=nas_eventfd_read_plan,.write_plan=nas_eventfd_write_plan,
    .read=nas_eventfd_read,.write=nas_eventfd_write,
    .atomic_write_limit=8,.read_commit_before_copyout=true,
    .aggregate_readv=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=nas_eventfd_poll,.poll_description=nas_eventfd_poll_slot,.release=vrelease_eventfd
};
static const struct nas_file_ops ops_timerfd={
    .read_plan=nas_timerfd_read_plan,.read=nas_timerfd_read,
    .read_commit_before_copyout=true,.aggregate_readv=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=nas_timerfd_poll_fd,.poll_description=nas_timerfd_poll_slot,
    .release=nas_timerfd_release
};
static uint64_t vioctl_inotify(uint64_t fd,uint64_t cmd,uint64_t arg) {
    if(cmd!=0x541bULL)return (uint64_t)-ENOTTY; /* Linux FIONREAD */
    const unsigned slot=TASK.fds->entries[fd].slot;
    uint32_t available=0;
    int rc=nas_inotify_pending_bytes(slot,&available);
    if(rc)return (uint64_t)(int64_t)rc;
    return nas_user_write_bytes(arg,(const uint8_t *)&available,
                                sizeof(available)) ? 0:(uint64_t)-EFAULT;
}
static const struct nas_file_ops ops_inotify={
    .read_plan=nas_inotify_read_plan,.read=nas_inotify_read,
    .read_variable_records=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=nas_inotify_poll_fd,.poll_description=nas_inotify_poll_slot,
    .ioctl=vioctl_inotify,
    .release=nas_inotify_release
};
static const struct nas_file_ops ops_signalfd={
    .read_plan=signalfd_read_plan,.read=signalfd_read,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll=signalfd_poll_fd,.poll_description=signalfd_poll_slot,
    .poll_description_task=nas_fs_signalfd_poll_task,
    .release=signalfd_release
};
static const struct nas_file_ops ops_pidfd={
    .read=nas_pidfd_read,
    .poll=nas_pidfd_poll_fd,
    .poll_description=nas_pidfd_poll_description,
    .release=nas_pidfd_release
};
#if defined(NAS_ENABLE_VIRTIO_INPUT) || \
    defined(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) || \
    defined(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) || \
    defined(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND)
static const struct nas_file_ops ops_evdev={
    .read_plan=nas_input_chrdev_read_plan,
    .read_record_bytes=sizeof(struct nas_input_event),
    .read=nas_input_chrdev_read,
    .aggregate_readv=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .poll_description=nas_input_chrdev_poll,
    .ioctl=nas_input_chrdev_ioctl,
    .release=nas_input_chrdev_release
};
#endif
#ifdef NAS_ENABLE_H15_QEMU_USB_SERIAL_USERLAND
static const struct nas_file_ops ops_usb_serial={
    .read_plan=nas_usb_serial_chrdev_read_plan,
    .read=nas_usb_serial_chrdev_read,
    .write=nas_usb_serial_chrdev_write,
    .aggregate_readv=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL_EAGAIN|NAS_IO_WAIT_RETRY,
    .poll_description=nas_usb_serial_chrdev_poll,
    .ioctl=nas_usb_serial_chrdev_ioctl,
    .release=nas_usb_serial_chrdev_release
};
#endif
static const struct nas_file_ops *const ops_by_kind[]={
    [FD_UART]=&ops_uart,[FD_TTY]=&ops_tty,[FD_NULL]=&ops_null,
    [FD_VT]=&ops_vt,
    [FD_ZERO]=&ops_zero,[FD_RANDOM]=&ops_random,
    [FD_BOOTFS]=&ops_bootfs,
    [FD_FRAMEBUFFER]=&ops_fbdev,
#if defined(NAS_ENABLE_VIRTIO_INPUT) || \
    defined(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) || \
    defined(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) || \
    defined(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND)
    [FD_EVDEV]=&ops_evdev,
#endif
#ifdef NAS_ENABLE_H15_QEMU_USB_SERIAL_USERLAND
    [FD_USB_SERIAL]=&ops_usb_serial,
#endif
    [FD_ARCHIVE]=&ops_vfs_regular,[FD_DIR]=&ops_vfs_dir,
    [FD_PIPE_R]=&ops_pipe_read,[FD_PIPE_W]=&ops_pipe_write,
    [FD_FIFO_RDWR]=&ops_fifo_rw,
    [FD_EPOLL]=&ops_epoll,
    [FD_EVENTFD]=&ops_eventfd,[FD_SIGNALFD]=&ops_signalfd,
    [FD_TIMERFD]=&ops_timerfd,
    [FD_INOTIFY]=&ops_inotify,
    [FD_PIDFD]=&ops_pidfd,
    [FD_SOCKET]=&ops_socket,
    [FD_PTY_MASTER]=&ops_pty,[FD_PTY_SLAVE]=&ops_pty
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
    nas_fs_path_end(&request);
    return result;
}
/* One common Linux xattr adapter for path/nofollow/File operations on all
 * three architectures. Namei resolves an owned Dentry; the VFS validates
 * the mount incarnation and the provider mutates the stable inode. The
 * syscall boundary alone touches userspace or Linux ABI argument order. */
static uint64_t sys_xattr(enum nas_syscall_op op,const uint64_t *args) {
    bool by_fd=op==NAS_SC_fsetxattr || op==NAS_SC_fgetxattr ||
               op==NAS_SC_flistxattr || op==NAS_SC_fremovexattr;
    bool nofollow=op==NAS_SC_lsetxattr || op==NAS_SC_lgetxattr ||
                  op==NAS_SC_llistxattr || op==NAS_SC_lremovexattr;
    bool setting=op==NAS_SC_setxattr || op==NAS_SC_lsetxattr ||
                 op==NAS_SC_fsetxattr;
    bool getting=op==NAS_SC_getxattr || op==NAS_SC_lgetxattr ||
                 op==NAS_SC_fgetxattr;
    bool listing=op==NAS_SC_listxattr || op==NAS_SC_llistxattr ||
                 op==NAS_SC_flistxattr;
    struct nas_fs_path_request request={0};
    struct nas_vfs_path path={0};
    struct nas_fs_node resolved={0};
    const struct nas_fs_node *node=NULL;
    int rc=0;
    if(by_fd) {
        if(args[1]>=NAS_FD_LIMIT || TASK.fds->entries[args[1]].kind==FD_CLOSED)
            return (uint64_t)-EBADF;
        const struct nas_open_file *opened=
            &nas_open_files[TASK.fds->entries[args[1]].slot];
        if(!opened->vnode_pinned ||
           (opened->status_flags&NAS_LINUX_O_PATH))
            return (uint64_t)-EBADF;
        node=&opened->vnode;
    } else {
        rc=nas_fs_user_lookup_begin((uint64_t)-100,args[1],!nofollow,
                                    &request,&path,&resolved);
        if(rc)return (uint64_t)(int64_t)rc;
        node=&resolved;
    }
    char name[256];
    if(!listing) {
        rc=nas_user_read_cstr(args[2],name,sizeof(name));
        if(rc)goto out;
    }
    uint64_t user_value=listing ? args[2] : args[3];
    uint64_t user_size=listing ? args[3] : args[4];
    uint64_t frame=0;
    uint8_t *data=NULL;
    if(setting) {
        if(user_size>4096) {rc=-E2BIG;goto out;}
        if(args[5]>UINT32_MAX) {rc=-EINVAL;goto out;}
        if(user_size) {
            frame=nas_frame_alloc();
            if(!frame) {rc=-ENOMEM;goto out;}
            data=(uint8_t *)(uintptr_t)(nas_hhdm_offset()+frame);
            if(!nas_user_read_bytes(user_value,data,user_size)) {
                rc=-EFAULT;goto done;
            }
        }
        rc=nas_vfs_setxattr(node,name,data,user_size,(unsigned)args[5]);
    } else if(getting || listing) {
        int64_t required=getting ? nas_vfs_getxattr(node,name,NULL,0) :
                                   nas_vfs_listxattr(node,NULL,0);
        if(required<0) {rc=(int)required;goto out;}
        if(!user_size) {rc=(int)required;goto out;}
        if((uint64_t)required>user_size) {rc=-ERANGE;goto out;}
        if(required>4096) {rc=-E2BIG;goto out;}
        if(required) {
            if(!nas_user_prepare_write(user_value,(uint64_t)required)) {
                rc=-EFAULT;goto out;
            }
            frame=nas_frame_alloc();
            if(!frame) {rc=-ENOMEM;goto out;}
            data=(uint8_t *)(uintptr_t)(nas_hhdm_offset()+frame);
            int64_t filled=getting ?
                nas_vfs_getxattr(node,name,data,(uint64_t)required) :
                nas_vfs_listxattr(node,data,(uint64_t)required);
            if(filled<0) {rc=(int)filled;goto done;}
            if(filled>required) {rc=-EIO;goto done;}
            if(!nas_user_write_bytes(user_value,data,(uint64_t)filled)) {
                rc=-EFAULT;goto done;
            }
            rc=(int)filled;
        }
    } else {
        rc=nas_vfs_removexattr(node,name);
    }
done:
    if(frame)(void)nas_frame_put(frame);
out:
    if(path.dentry && nas_vfs_path_put(&path))
        nas_fs_fail("xattr Dentry reference corrupted");
    if(!by_fd)nas_fs_path_end(&request);
    return (uint64_t)(int64_t)rc;
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
    case NAS_SC_timerfd_create:
        FS_RESULT(nas_timerfd_create(args[1],args[2]));
    case NAS_SC_timerfd_settime:
        FS_RESULT(nas_timerfd_settime(args[1],args[2],args[3],args[4]));
    case NAS_SC_timerfd_gettime:
        FS_RESULT(nas_timerfd_gettime(args[1],args[2]));
    case NAS_SC_inotify_init:
        FS_RESULT(nas_inotify_init1(0));
    case NAS_SC_inotify_init1:
        FS_RESULT(nas_inotify_init1(args[1]));
    case NAS_SC_inotify_add_watch:
        FS_RESULT(nas_inotify_add_watch(args[1],args[2],args[3]));
    case NAS_SC_inotify_rm_watch:
        FS_RESULT(nas_inotify_rm_watch(args[1],args[2]));
    case NAS_SC_signalfd4:
        FS_RESULT(sys_signalfd4(args[1],args[2],args[3],args[4]));
    case NAS_SC_epoll_create1:
        FS_RESULT(nas_epoll_create1(args[1]));
    case NAS_SC_epoll_create:
        FS_RESULT(args[1] ? nas_epoll_create1(0) : (uint64_t)-EINVAL);
    case NAS_SC_epoll_ctl:
        FS_RESULT(nas_epoll_ctl(args[1],args[2],args[3],args[4]));
    case NAS_SC_pidfd_open:
        FS_RESULT(nas_pidfd_open(args[1],args[2]));
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
    case NAS_SC_setxattr: case NAS_SC_lsetxattr: case NAS_SC_fsetxattr:
    case NAS_SC_getxattr: case NAS_SC_lgetxattr: case NAS_SC_fgetxattr:
    case NAS_SC_listxattr: case NAS_SC_llistxattr: case NAS_SC_flistxattr:
    case NAS_SC_removexattr: case NAS_SC_lremovexattr: case NAS_SC_fremovexattr:
        FS_RESULT(sys_xattr(op,args));
    case NAS_SC_dup3:
        if (args[1] == args[2] || (args[3] & ~0x80000ULL))
            FS_RESULT((uint64_t)-EINVAL);
        FS_RESULT(fd_clone(args[1], args[2], (args[3] & 0x80000ULL) != 0));
    default:
        return (struct nas_fs_result){ .handled = false, .value = 0 };
    }
}
#undef FS_RESULT
