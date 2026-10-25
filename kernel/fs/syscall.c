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
#include "file_ops.h"
#include "epoll.h"
#include "eventfd.h"
#include "unix_socket.h"
#include "lookup.h"
#include "../time/clock.h"
/* Linux x86-64 filesystem syscall adapter. Backend state is already
 * task-local or refcounted via fd.c, pipe.c, bootfs.c and tmpfs.c.
 * The user ABI stat/dirent encodings are still x86-specific in v1. */
#define TASK (*nas_current_task)
#define descriptors TASK.fds->descriptors
#define file_slot TASK.fds->file_slot
#define fd_cloexec TASK.fds->fd_cloexec
#define current_workdir TASK.vm_owner->cwd
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
static struct nas_tmp_file *tmp_by_fd(uint64_t fd) {
    return tmp_by_slot(file_slot[fd]);
}

/* Same open-file-description source for read(), exec and private mmap.
 * Do not expose kernel pointer paths or share mutable file offsets. */
int64_t nas_fs_mmap_read(uint64_t fd, uint64_t offset,
                        uint8_t *buffer, uint64_t length) {
    if (fd >= NAS_FD_LIMIT || descriptors[fd] == FD_CLOSED)
        return -EBADF;
    unsigned slot = file_slot[fd];
    if (slot >= NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    if(nas_open_files[slot].status_flags & NAS_LINUX_O_PATH)return -EBADF;
    if ((nas_open_files[slot].status_flags & 3ULL) == 1ULL)
        return -EACCES;
    const uint8_t *bytes = 0;
    uint64_t size = 0;
    if (descriptors[fd] == FD_ARCHIVE) {
        const struct bootfs_entry *entry = nas_open_files[slot].boot_entry;
        if (!entry || bootfs_is_dir(entry)) return -ENODEV;
        bytes = entry->data;
        size = entry->size;
    } else if (descriptors[fd] == FD_BOOTFS) {
        const struct limine_file *file = nas_open_files[slot].boot_file;
        if (!file || !file->address) return -EBADF;
        bytes = (const uint8_t *)file->address;
        size = file->size;
    } else if (descriptors[fd] == FD_TMP) {
        const struct nas_tmp_file *file = tmp_by_slot(slot);
        if (!file || file->is_dir) return -ENODEV;
        return tmp_read_at(file, offset, buffer, length);
    } else {
        return -ENODEV;
    }
    if (offset >= size) return 0;
    if (length > size-offset) length = size-offset;
    if (length && !buffer) return -EFAULT;
    if (length) memcpy(buffer, bytes+offset, (size_t)length);
    return (int64_t)length;
}

/* The two endpoints are one pipe object, with separate shared-description
 * slots. Allocation remains transactional until both FD/slot pairs are
 * available; only then is the backend created. */
static uint64_t sys_channel_pair(uint64_t user_pair, uint64_t flags,
                                 bool stream_socket) {
    if (user_pair >= USER_LIMIT || user_pair > USER_LIMIT - 8)
        return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < 8; i++)
        if (!nas_mmu_user_byte(user_pair + i)) return (uint64_t)-EFAULT;
    unsigned fds[2], slots[2], nfds = 0, nslots = 0;
    for (unsigned fd = 3; fd < NAS_FD_LIMIT && nfds < 2; fd++)
        if (descriptors[fd] == FD_CLOSED) fds[nfds++] = fd;
    if (nfds != 2) return (uint64_t)-EMFILE;
    for (unsigned slot = 0; slot < NAS_OPEN_FILE_LIMIT && nslots < 2; slot++)
        if (!nas_open_files[slot].refcount) slots[nslots++] = slot;
    if (nslots != 2) {
        nas_fd_report_exhaustion("pipe2");
        return (uint64_t)-ENFILE;
    }
    int result = stream_socket ?
        nas_pipe_create_socketpair(slots[0], slots[1]) :
        nas_pipe_create(slots[0], slots[1]);
    if (result) return (uint64_t)(int64_t)result;
    for (unsigned i = 0; i < 2; i++) {
        unsigned fd = fds[i], slot = slots[i];
        descriptors[fd] = stream_socket ? FD_SOCKET :
                          i ? FD_PIPE_W : FD_PIPE_R;
        file_slot[fd] = (uint8_t)slot;
        nas_open_files[slot].refcount = 1;
        nas_open_files[slot].ops = nas_fs_ops_for_kind(descriptors[fd]);
        nas_open_files[slot].status_flags = (stream_socket ? 2ULL :
                            i ? 1ULL : 0ULL) | (flags & 0x800ULL);
        fd_cloexec[fd] = !!(flags & 0x80000ULL);
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
/* AF_UNIX SOCK_STREAM: same global refcounted open descriptions as pipe2
 * and socketpair. A listener owns pending accepted slots, never a task-local
 * FD until accept transfers ownership. All ISA-specific numbers are decoded
 * above this shared implementation. */
static bool unix_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && descriptors[fd]==FD_SOCKET &&
           file_slot[fd]<NAS_OPEN_FILE_LIMIT;
}
static uint64_t sys_socket(uint64_t domain,uint64_t type,uint64_t protocol) {
    if(domain!=1)return (uint64_t)-EAFNOSUPPORT;
    if((type&15U)!=1U || (type&~(15ULL|0x800ULL|0x80000ULL)))
        return (uint64_t)-ESOCKTNOSUPPORT;
    if(protocol)return (uint64_t)-EPROTONOSUPPORT;
    unsigned fd=NAS_FD_LIMIT,slot=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=3;i<NAS_FD_LIMIT;i++)
        if(descriptors[i]==FD_CLOSED) {fd=i;break;}
    if(fd==NAS_FD_LIMIT)return (uint64_t)-EMFILE;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++)
        if(!nas_open_files[i].refcount) {slot=i;break;}
    if(slot==NAS_OPEN_FILE_LIMIT)return (uint64_t)-ENFILE;
    nas_unix_socket_init(slot);
    nas_open_files[slot]=(struct nas_open_file){
        .refcount=1,.status_flags=2ULL|(type&0x800ULL),
        .ops=nas_fs_ops_for_kind(FD_SOCKET)
    };
    descriptors[fd]=FD_SOCKET;
    file_slot[fd]=(uint8_t)slot;
    fd_cloexec[fd]=(type&0x80000ULL)!=0;
    return fd;
}
/* Linux sockaddr_un has a native two-byte family followed by the pathname.
 * This first backend supports Linux pathname sockets, not abstract names. */
static int unix_address(uint64_t addr,uint64_t length,char out[256]) {
    uint8_t buffer[110];
    if(length<3 || length>sizeof(buffer))return -EINVAL;
    if(!nas_user_read_bytes(addr,buffer,length))return -EFAULT;
    if(buffer[0]!=1 || buffer[1])return -EAFNOSUPPORT;
    if(!buffer[2])return -EOPNOTSUPP;
    char input[109];
    unsigned n=0;
    for(;n<length-2 && buffer[n+2] && n<sizeof(input)-1;n++)
        input[n]=(char)buffer[n+2];
    input[n]=0;
    return nas_path_resolve(input[0]=='/' ? 0 : current_workdir,input,out);
}
static uint64_t sys_unix_bind(uint64_t fd,uint64_t address,uint64_t size) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    char path[256];int err=unix_address(address,size,path);
    if(err)return (uint64_t)(int64_t)err;
    return (uint64_t)(int64_t)nas_unix_socket_bind(
        file_slot[fd],path,0777U & ~(uint32_t)TASK.vm_owner->umask);
}
static uint64_t sys_unix_listen(uint64_t fd,uint64_t backlog) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    return (uint64_t)(int64_t)nas_unix_socket_listen(file_slot[fd],
                                                 backlog>16?16U:(unsigned)backlog);
}
static uint64_t sys_unix_connect(uint64_t fd,uint64_t address,uint64_t size) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    char path[256];int err=unix_address(address,size,path);
    if(err)return (uint64_t)(int64_t)err;
    return (uint64_t)(int64_t)nas_unix_socket_connect(file_slot[fd],path);
}
static uint64_t sys_unix_accept(uint64_t fd,uint64_t address,
                                 uint64_t length_ptr,uint64_t flags) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    if(flags & ~(0x800ULL|0x80000ULL))return (uint64_t)-EINVAL;
    if((address&&!length_ptr)||(!address&&length_ptr))return (uint64_t)-EFAULT;
    uint32_t length=0;
    if(length_ptr && !nas_user_read_bytes(length_ptr,(uint8_t *)&length,4))
        return (uint64_t)-EFAULT;
    if(address && length && (!nas_mmu_user_byte(address) ||
                             (length>1 && !nas_mmu_user_byte(address+1))))
        return (uint64_t)-EFAULT;
    unsigned nextfd=NAS_FD_LIMIT;
    for(unsigned i=3;i<NAS_FD_LIMIT;i++)
        if(descriptors[i]==FD_CLOSED) {nextfd=i;break;}
    if(nextfd==NAS_FD_LIMIT)return (uint64_t)-EMFILE;
    unsigned slot;
    int rc=nas_unix_socket_accept(file_slot[fd],&slot);
    if(rc)return (uint64_t)(int64_t)rc;
    descriptors[nextfd]=FD_SOCKET;
    file_slot[nextfd]=(uint8_t)slot;
    fd_cloexec[nextfd]=(flags&0x80000ULL)!=0;
    nas_open_files[slot].status_flags|=flags&0x800ULL;
    if(length_ptr) {
        uint32_t actual=2;
        uint8_t family[2]={1,0};
        if((length && !nas_user_write_bytes(address,family,length>2?2:length)) ||
           !nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4))
            nas_fs_fail("validated accept address became inaccessible");
    }
    return nextfd;
}
static uint64_t sys_unix_name(uint64_t fd,uint64_t address,
                              uint64_t length_ptr,bool peer) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    if(!address || !length_ptr)return (uint64_t)-EFAULT;
    uint32_t size;
    if(!nas_user_read_bytes(length_ptr,(uint8_t *)&size,4))
        return (uint64_t)-EFAULT;
    uint8_t output[110]={1,0};
    const char *path=peer ? 0 : nas_unix_socket_name(file_slot[fd]);
    uint32_t actual=2;
    if(path) {
        unsigned n=(unsigned)strnlen(path,107);
        memcpy(output+2,path,n);
        output[n+2]=0;
        actual=n+3;
    }
    if(size>actual)size=actual;
    return nas_user_write_bytes(address,output,size) &&
           nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4)
         ? 0:(uint64_t)-EFAULT;
}
static uint64_t sys_unix_getsockopt(uint64_t fd,uint64_t level,
                                    uint64_t opt,uint64_t address,
                                    uint64_t size_ptr) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    if(level!=1)return (uint64_t)-ENOPROTOOPT;
    uint32_t size;
    if(!nas_user_read_bytes(size_ptr,(uint8_t *)&size,4))
        return (uint64_t)-EFAULT;
    if(size<4)return (uint64_t)-EINVAL;
    uint32_t value;
    if(opt==3)value=1; /* SO_TYPE: stream */
    else if(opt==4)value=0; /* SO_ERROR: no pending asynchronous error */
    else return (uint64_t)-ENOPROTOOPT;
    size=4;
    return nas_user_write_bytes(address,(const uint8_t *)&value,4) &&
           nas_user_write_bytes(size_ptr,(const uint8_t *)&size,4)
         ? 0:(uint64_t)-EFAULT;
}
/* The three Linux LP64 ISAs share the 56-byte msghdr/iovec wire layout.
 * Ancillary SCM_RIGHTS preserves open-description identity across processes,
 * including FD flags and pipe/PTY lifetime. Unknown cmsgs are rejected. */
struct nas_msghdr {
    uint64_t name;
    uint32_t namelen,pad;
    uint64_t iov,iovlen,control,controllen;
    uint32_t flags,pad2;
};
struct nas_iovec { uint64_t base,length; };
_Static_assert(sizeof(struct nas_msghdr)==56,"native Linux msghdr ABI");
static uint64_t unix_msgiov(uint64_t fd,const struct nas_msghdr *msg,
                            bool writing) {
    if(msg->iovlen>32)return (uint64_t)-EMSGSIZE;
    uint64_t total=0;
    for(unsigned i=0;i<msg->iovlen;i++) {
        struct nas_iovec v;
        if(msg->iov>UINT64_MAX-(uint64_t)i*sizeof(v) ||
           !nas_user_read_bytes(msg->iov+(uint64_t)i*sizeof(v),
                                (uint8_t *)&v,sizeof(v)))
            return total?total:(uint64_t)-EFAULT;
        if(!v.length)continue;
        uint64_t done=writing?nas_fs_fd_write(fd,v.base,v.length):
                              nas_fs_fd_read(fd,v.base,v.length);
        if((int64_t)done<0)return total?total:done;
        total+=done;
        if(done<v.length)break;
    }
    return total;
}
static uint64_t sys_unix_sendmsg(uint64_t fd,uint64_t address,uint64_t flags) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    if(flags & ~(0x4000ULL|0x40ULL))return (uint64_t)-EOPNOTSUPP;
    struct nas_msghdr msg;
    if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)))
        return (uint64_t)-EFAULT;
    if(msg.name)return (uint64_t)-EISCONN;
    uint8_t kinds[NAS_UNIX_MAX_RIGHTS],slots[NAS_UNIX_MAX_RIGHTS];
    unsigned rights_count=0;
    if(msg.controllen) {
        uint8_t control[16+NAS_UNIX_MAX_RIGHTS*4];
        if(!msg.control || msg.controllen<16 || msg.controllen>sizeof(control) ||
           !nas_user_read_bytes(msg.control,control,msg.controllen))
            return (uint64_t)-EINVAL;
        uint64_t cmsglen;
        memcpy(&cmsglen,control,8);
        uint32_t level,type;
        memcpy(&level,control+8,4);memcpy(&type,control+12,4);
        if(cmsglen<16 || cmsglen>msg.controllen || level!=1 || type!=1 ||
           (cmsglen-16)%4)return (uint64_t)-EOPNOTSUPP;
        rights_count=(unsigned)((cmsglen-16)/4);
        if(!rights_count || rights_count>NAS_UNIX_MAX_RIGHTS)
            return (uint64_t)-EINVAL;
        for(unsigned i=0;i<rights_count;i++) {
            int32_t transferred;
            memcpy(&transferred,control+16+i*4,4);
            if(transferred<0 || transferred>=NAS_FD_LIMIT ||
               descriptors[transferred]==FD_CLOSED)return (uint64_t)-EBADF;
            kinds[i]=descriptors[transferred];
            slots[i]=file_slot[transferred];
        }
        if(!nas_unix_rights_available(file_slot[fd],rights_count))
            return (uint64_t)-EAGAIN;
    }
    uint64_t wrote=unix_msgiov(fd,&msg,true);
    if((int64_t)wrote<0)return wrote;
    if(rights_count && wrote) {
        int rc=nas_unix_rights_send(file_slot[fd],kinds,slots,rights_count);
        if(rc)return (uint64_t)(int64_t)rc;
    }
    return wrote;
}
static uint64_t sys_unix_recvmsg(uint64_t fd,uint64_t address,uint64_t flags) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    if(flags & ~(0x40000000ULL|0x40ULL|0x2ULL))
        return (uint64_t)-EOPNOTSUPP; /* CMSG_CLOEXEC/DONTWAIT/PEEK */
    if(flags&0x2ULL)return (uint64_t)-EOPNOTSUPP;
    struct nas_msghdr msg;
    if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)))
        return (uint64_t)-EFAULT;
    if(msg.iovlen>32)return (uint64_t)-EMSGSIZE;
    unsigned pending=nas_unix_rights_count(file_slot[fd]);
    unsigned deliver=0;
    if(msg.control && msg.controllen>=20)
        deliver=(unsigned)((msg.controllen-16)/4);
    if(deliver>pending)deliver=pending;
    if(deliver>NAS_UNIX_MAX_RIGHTS)deliver=NAS_UNIX_MAX_RIGHTS;
    unsigned free_fd=0;
    for(unsigned i=3;i<NAS_FD_LIMIT;i++)
        if(descriptors[i]==FD_CLOSED)free_fd++;
    if(deliver>free_fd)return (uint64_t)-EMFILE;
    uint64_t received=unix_msgiov(fd,&msg,false);
    if((int64_t)received<0)return received;
    msg.namelen=0;
    msg.flags=pending>deliver ? 0x8U : 0U; /* MSG_CTRUNC */
    msg.controllen=0;
    if(deliver && received) {
        uint8_t kinds[NAS_UNIX_MAX_RIGHTS],slots[NAS_UNIX_MAX_RIGHTS];
        unsigned count=nas_unix_rights_receive(file_slot[fd],kinds,slots,deliver);
        uint8_t control[16+NAS_UNIX_MAX_RIGHTS*4]={0};
        uint64_t clen=16+(uint64_t)count*4;
        uint32_t level=1,type=1;
        memcpy(control,&clen,8);
        memcpy(control+8,&level,4);memcpy(control+12,&type,4);
        unsigned done=0;
        for(unsigned i=3;i<NAS_FD_LIMIT && done<count;i++) {
            if(descriptors[i]!=FD_CLOSED)continue;
            descriptors[i]=kinds[done];
            file_slot[i]=slots[done];
            fd_cloexec[i]=(flags&0x40000000ULL)!=0;
            int32_t number=(int32_t)i;
            memcpy(control+16+done*4,&number,4);
            done++;
        }
        msg.controllen=(clen+7ULL)&~7ULL;
        if(!nas_user_write_bytes(msg.control,control,(size_t)clen))
            nas_fs_fail("recvmsg validated control became inaccessible");
    }
    if(!nas_user_write_bytes(address,(const uint8_t *)&msg,sizeof(msg)))
        return (uint64_t)-EFAULT;
    return received;
}
static uint64_t pipe_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    unsigned slot = file_slot[fd];
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
    unsigned slot = file_slot[fd];
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
void nas_fs_close_fd(uint64_t fd) {
    /* Linux POSIX locks are PROCESS-owned and closing even one descriptor
     * releases that process's locks for the whole inode (including dup). */
    if(fd<NAS_FD_LIMIT && descriptors[fd]==FD_TMP) {
        struct nas_tmp_file *file=tmp_by_fd(fd);
        if(file)nas_locks_release(&file->posix_locks,TASK.pid);
    }
    unsigned final_slot=nas_fd_close(nas_current_task, (unsigned)fd);
    if (final_slot == NAS_OPEN_FILE_LIMIT) return;
    /* Closing the LAST alias unregisters epoll interest in the released
     * open description, preserving the open-file lifetime rule. */
    nas_epoll_target_final_close(final_slot);
    const struct nas_file_ops *ops=nas_open_files[final_slot].ops;
    if(ops && ops->release)ops->release(final_slot);
    nas_open_files[final_slot]=(struct nas_open_file){0};
}
static uint64_t fd_clone(uint64_t src, uint64_t dst, bool cloexec) {
    if (src >= NAS_FD_LIMIT || descriptors[src] == FD_CLOSED ||
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
    if(descriptors[fd]!=FD_TMP)return (uint64_t)-EINVAL;
    struct nas_tmp_file *inode=tmp_by_fd(fd);
    if(!inode || inode->is_dir)return (uint64_t)-EBADF;
    struct nas_flock64 lk={0};
    if(!nas_user_read_bytes(user,(uint8_t *)&lk,sizeof(lk)))
        return (uint64_t)-EFAULT;
    if(lk.type<0 || lk.type>2 || lk.whence<0 || lk.whence>2)
        return (uint64_t)-EINVAL;
    uint64_t base=0,position=nas_open_files[file_slot[fd]].position;
    if(lk.whence==1)base=position;
    if(lk.whence==2)base=inode->size;
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
        unsigned access=(unsigned)(nas_open_files[file_slot[fd]].status_flags&3ULL);
        if((lk.type==NAS_LOCK_READ && access==1) ||
           (lk.type==NAS_LOCK_WRITE && access==0))return (uint64_t)-EBADF;
    }
    if(command==5) {
        if(lk.type==NAS_LOCK_UNLOCK)return (uint64_t)-EINVAL;
        int conflict=nas_locks_conflict(&inode->posix_locks,TASK.pid,
                                     (uint16_t)lk.type,start,end);
        if(conflict<0)lk.type=NAS_LOCK_UNLOCK;
        else {
            const struct nas_posix_lock *old=&inode->posix_locks.entries[conflict];
            lk.type=(int16_t)old->type;
            lk.whence=0;
            lk.start=(int64_t)old->start;
            lk.length=old->end==UINT64_MAX ? 0 : (int64_t)(old->end-old->start);
            lk.pid=(int32_t)old->pid;
        }
        return nas_user_write_bytes(user,(uint8_t *)&lk,sizeof(lk)) ?
               0 : (uint64_t)-EFAULT;
    }
    int rc=nas_locks_apply(&inode->posix_locks,TASK.pid,
                           (uint16_t)lk.type,start,end);
    if(rc==1)return (uint64_t)-EAGAIN; /* non-blocking lock conflict */
    if(rc==2)return (uint64_t)-ENOLCK;
    return rc ? (uint64_t)-EINVAL : 0;
}
static uint64_t sys_fcntl(uint64_t fd, uint64_t command, uint64_t arg) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED)
        return (uint64_t)-EBADF;
    unsigned slot = file_slot[fd];
    switch (command) {
    case 0: return fd_duplicate(fd, arg, false);  /* F_DUPFD */
    case 1: return fd_cloexec[fd];                 /* F_GETFD */
    case 2: fd_cloexec[fd] = (uint8_t)(arg & 1); return 0; /* F_SETFD */
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
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED)return (uint64_t)-EBADF;
    unsigned slot=file_slot[fd];
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
        fd_cloexec[fd]=(uint8_t)(cmd==0x5451ULL);
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

/* Lexically combine absolute, cwd-relative and dirfd-relative paths.
 * bootfs_resolve performs archive symlink traversal at the actual lookup.
 */
/* This adapter owns user memory and dirfd lookup. Component processing
 * lives in path.c and can be tested independently of the syscall ABI. */
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
    if (src[0]!='/') {
        if (dirfd==(uint64_t)-100) base=current_workdir;
        else {
            if (dirfd>=NAS_FD_LIMIT || descriptors[dirfd]==FD_CLOSED)
                return (uint64_t)-EBADF;
            if (descriptors[dirfd]==FD_TMP_DIR) {
                const struct nas_tmp_file *dir=tmp_by_fd(dirfd);
                if (!dir) return (uint64_t)-EBADF;
                base=dir->path;
            } else {
                if (descriptors[dirfd]!=FD_DIR || !nas_open_files[file_slot[dirfd]].boot_entry)
                    return (uint64_t)-ENOTDIR;
                base=nas_open_files[file_slot[dirfd]].boot_entry->path;
            }
        }
    }
    return (uint64_t)(int64_t)nas_path_resolve(base,src,out);
}

/* A bounded adapter transfers user buffers to the shared page-backed tmpfs.
 * Kernel filesystem storage knows nothing about user virtual addresses. */
static uint64_t tmp_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    const struct nas_tmp_file *file = tmp_by_fd(fd);
    if (!file) return (uint64_t)-EBADF;
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    unsigned slot = file_slot[fd];
    uint64_t offset = nas_open_files[slot].position, done = 0;
    uint8_t buffer[1024];
    while (done < length) {
        uint64_t n = length - done;
        if (n > sizeof(buffer)) n = sizeof(buffer);
        int64_t got = tmp_read_at(file, offset + done, buffer, n);
        if (got < 0) return done ? done : (uint64_t)got;
        if (!got) break;
        if (!nas_user_write_bytes(address + done, buffer, (uint64_t)got))
            return done ? done : (uint64_t)-EFAULT;
        done += (uint64_t)got;
        nas_open_files[slot].position = offset + done;
    }
    return done;
}
static uint64_t tmp_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    unsigned slot = file_slot[fd];
    struct nas_tmp_file *file = tmp_by_slot(slot);
    if (!file) return (uint64_t)-EBADF;
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    uint64_t offset = (nas_open_files[slot].status_flags & 0x400ULL) ?
                      file->size : nas_open_files[slot].position;
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
        int64_t written = tmp_write_at(file, offset + done, buffer, n);
        if (written < 0) return done ? done : (uint64_t)written;
        done += (uint64_t)written;
        nas_open_files[slot].position = offset + done;
        if ((uint64_t)written != n) break;
        if (n < sizeof(buffer) && done < length) break; /* user fault */
    }
    nas_open_files[slot].position = offset + done;
    return done;
}
/* Linux syscall decoders retain user-pointer validation and task-relative
 * path resolution; the RAM filesystem backend owns namespace operations. */
static uint64_t sys_mkdirat_tmp(uint64_t dirfd, uint64_t pathname,
                                uint64_t mode) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_mkdir(path,
                            nas_fs_create_mode(mode,TASK.vm_owner->umask));
}
static uint64_t sys_unlinkat_tmp(uint64_t dirfd, uint64_t pathname,
                                 uint64_t flags) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_unlink(path, flags);
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
        (uint64_t)(int64_t)tmp_rename(oldpath, newpath, flags);
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
        (uint64_t)(int64_t)tmp_symlink(target,linkpath);
}
static uint64_t sys_open_path(const char *path, uint64_t native_flags,
                              uint64_t create_mode) {
    uint64_t flags;
    if (!nas_linux_open_flags(native_flags, &flags)) return (uint64_t)-EINVAL;
    flags=nas_linux_path_only_flags(flags);
    const uint64_t permitted = NAS_LINUX_O_PATH | 3ULL | 0x800ULL | 0x8000ULL |
                               0x10000ULL | 0x20000ULL | 0x80000ULL | 0x100ULL |
                               0x80ULL | /* O_EXCL */
                               0x40ULL | 0x200ULL | 0x400ULL;
    if (flags & ~permitted) return (uint64_t)-EINVAL;
    if((strcmp(path, "/dev/tty") == 0) &&
       !nas_tty_controlling() && !nas_pty_controlling())
        return (uint64_t)-ENXIO;
    uint8_t kind = FD_CLOSED;
    const struct bootfs_entry *entry = 0;
    const struct limine_file *real_file = 0;
    struct nas_tmp_file *tmp_file = 0;
    bool create_tmp = false;
    int pty_number=nas_pty_path_number(path);
    if(!strcmp(path,"/dev/ptmx"))kind=FD_PTY_MASTER;
    else if(pty_number>=0 && nas_pty_present((unsigned)pty_number))
        kind=FD_PTY_SLAVE;
    else if(!strcmp(path,"/dev/tty") && nas_pty_controlling())
        kind=FD_PTY_SLAVE;
    else if((strcmp(path, "/dev/tty") == 0) || (strcmp(path, "/dev/console") == 0) ||
       (strcmp(path, "/dev/ttyS0") == 0))kind=FD_TTY;
    else if ((strcmp(path, "/dev/null") == 0)) kind = FD_NULL;
    else if ((strcmp(path, "/dev/zero") == 0)) kind = FD_ZERO;
    else {
        if (bootfs_mounted()) {
            char actual[256];
            int err = nas_fs_resolve_existing(path, !(flags & 0x20000ULL), actual);
            if (!err) {
                tmp_file=tmp_find(actual);
                if (tmp_file) {
                    if (tmp_is_link(tmp_file) &&
                        !((flags & NAS_LINUX_O_PATH) && (flags & 0x20000ULL)))
                        return (uint64_t)-ELOOP;
                    kind=tmp_file->is_dir ? FD_TMP_DIR : FD_TMP;
                } else {
                    entry = bootfs_find(actual);
                    if (bootfs_is_link(entry) &&
                        !((flags & NAS_LINUX_O_PATH) && (flags & 0x20000ULL)))
                        return (uint64_t)-ELOOP;
                    kind = bootfs_is_dir(entry) ? FD_DIR : FD_ARCHIVE;
                }
            } else if (err != -ENOENT) {
                return (uint64_t)(int64_t)err;
            }
        }
        if (!kind && running_alpine && (real_file = bootfs_lookup(path)))
            kind = FD_BOOTFS; /* Legacy M4 module mapping. */
        if (!kind && bootfs_mounted() && tmp_path(path) &&
            bootfs_find("/tmp")) {
            tmp_file = tmp_find(path);
            if (tmp_file) kind = tmp_file->is_dir ? FD_TMP_DIR : FD_TMP;
            else {
                int parent = tmp_parent_check(path);
                if (parent) return (uint64_t)(int64_t)parent;
                if (flags & 0x40ULL) { kind = FD_TMP; create_tmp = true; }
            }
        }
    }
    if (!kind) return (flags & 0x40ULL) && bootfs_mounted()
        ? (uint64_t)-EROFS : (uint64_t)-ENOENT;
    /* Unix pathname sockets have genuine S_IFSOCK in tmpfs, but unlike a
     * regular inode cannot be opened as a byte file (O_PATH may inspect). */
    if(tmp_file && (tmp_file->mode&0170000U)==0140000U &&
       !(flags&NAS_LINUX_O_PATH))return (uint64_t)-ENXIO;
    if ((flags & 0x10000ULL) && kind != FD_DIR && kind != FD_TMP_DIR)
        return (uint64_t)-ENOTDIR;
    if (kind == FD_TMP_DIR && ((flags & 3ULL) || (flags & 0x200ULL)))
        return (uint64_t)-EISDIR;
    if (kind == FD_TMP) {
        if (tmp_file && (flags & 0xc0ULL) == 0xc0ULL)
            return (uint64_t)-EEXIST;
        if ((flags & 0x200ULL) && !(flags & 3ULL))
            return (uint64_t)-EINVAL;
    }
    /* O_CREAT on an existing /dev/null or /dev/zero is not a request
     * to create a new file. O_TRUNC has no effect on character devices.
     * Archive-backed data, however, stays strictly read-only.
     */
    /* O_CREAT means "create if absent", not "write". A pre-existing
     * immutable CPIO regular file can be opened O_RDONLY|O_CREAT.
     * O_CREAT|O_EXCL must refuse an existing name with EEXIST.
     */
    if ((flags & 0xc0ULL) == 0xc0ULL &&
        (kind == FD_ARCHIVE || kind == FD_DIR || kind == FD_BOOTFS))
        return (uint64_t)-EEXIST;
    if (kind != FD_NULL && kind != FD_ZERO && kind != FD_TTY &&
        kind != FD_PTY_MASTER && kind != FD_PTY_SLAVE &&
        kind != FD_TMP &&
        (flags & 0x200ULL))
        return (uint64_t)-EROFS;
    if (kind == FD_BOOTFS && (flags & (0x40ULL | 0x400ULL)))
        return (uint64_t)-EROFS;
    if ((kind == FD_ARCHIVE || kind == FD_DIR) && (flags & 3ULL))
        return kind == FD_DIR ? (uint64_t)-EISDIR : (uint64_t)-EROFS;
    if (kind == FD_BOOTFS && (flags & 3ULL)) return (uint64_t)-EACCES;
    for (unsigned fd = 3; fd < NAS_FD_LIMIT; fd++) {
        if (descriptors[fd] != FD_CLOSED) continue;
        for (unsigned slot = 0; slot < NAS_OPEN_FILE_LIMIT; slot++) {
            if (nas_open_files[slot].refcount) continue;
            if (create_tmp) {
                tmp_file = tmp_create(path,false,
                            nas_fs_create_mode(create_mode,TASK.vm_owner->umask));
                if (!tmp_file) return (uint64_t)-ENOSPC;
            }
            if ((kind == FD_TMP || kind == FD_TMP_DIR) && !tmp_file) return (uint64_t)-ENOENT;
            if (kind == FD_TMP && (flags & 0x200ULL)) tmp_truncate(tmp_file);
            if (kind == FD_PTY_MASTER) {
                int err=nas_pty_new(slot);
                if(err)return (uint64_t)(int64_t)err;
            } else if (kind == FD_PTY_SLAVE) {
                int err=pty_number>=0 ?
                    nas_pty_attach((unsigned)pty_number,slot) :
                    nas_pty_attach_controlling(slot);
                if(err)return (uint64_t)(int64_t)err;
            }
            nas_open_files[slot].refcount = 1;
            nas_open_files[slot].ops = nas_fs_ops_for_kind(kind);
            if (kind == FD_TMP || kind == FD_TMP_DIR)
                tmp_bind_slot(slot, tmp_file);
            file_slot[fd] = (uint8_t)slot;
            nas_open_files[slot].status_flags =
                flags & (NAS_LINUX_O_PATH | 3ULL | 0x800ULL | 0x400ULL);
            fd_cloexec[fd] = !!(flags & 0x80000ULL);
            nas_open_files[slot].position = 0;
            descriptors[fd] = kind;
            nas_open_files[file_slot[fd]].boot_file = real_file;
            nas_open_files[file_slot[fd]].boot_entry = entry;
            return fd;
        }
        nas_fd_report_exhaustion("openat");
        return (uint64_t)-ENFILE;
    }
    return (uint64_t)-EMFILE;
}
static uint64_t sys_open_device(uint64_t pathname, uint64_t flags,
                                 uint64_t mode) {
    char path[256];
    uint64_t result = nas_fs_resolve_path((uint64_t)-100, pathname, path);
    return (int64_t)result < 0 ? result : sys_open_path(path,flags,mode);
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
static uint64_t vread_archive(uint64_t fd,uint64_t addr,uint64_t n) {
    const struct bootfs_entry *entry=nas_open_files[file_slot[fd]].boot_entry;
    if(!entry)return (uint64_t)-EBADF;
    uint64_t start=nas_open_files[file_slot[fd]].position;
    if(start>=entry->size)return 0;
    if(n>entry->size-start)n=entry->size-start;
    if(!nas_user_write_bytes(addr,entry->data+start,n))
        return (uint64_t)-EFAULT;
    nas_open_files[file_slot[fd]].position+=n;
    return n;
}
static uint64_t vread_limine(uint64_t fd,uint64_t addr,uint64_t n) {
    const struct limine_file *file=nas_open_files[file_slot[fd]].boot_file;
    if(!file || !file->address)return (uint64_t)-EBADF;
    uint64_t start=nas_open_files[file_slot[fd]].position;
    if(start>file->size)return 0;
    if(n>file->size-start)n=file->size-start;
    if(!nas_user_write_bytes(addr,(const uint8_t*)file->address+start,n))
        return (uint64_t)-EFAULT;
    nas_open_files[file_slot[fd]].position+=n;
    return n;
}
static uint64_t vread_tty(uint64_t fd,uint64_t addr,uint64_t n) {
    return nas_tty_read(addr,n,(nas_open_files[file_slot[fd]].status_flags&0x800ULL)!=0);
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
static uint16_t vpoll_always(uint64_t fd,uint16_t events) {
    (void)fd;return events & 0x0005U;
}
static uint16_t vpoll_tty(uint64_t fd,uint16_t events) {
    (void)fd;
    uint16_t out=events & 0x0004U;
    if((events & 0x0001U) && nas_tty_poll_readable())out|=0x0001U;
    return out;
}
static uint16_t vpoll_pipe_read(uint64_t fd,uint16_t events) {
    return nas_pipe_poll_readable(file_slot[fd]) ? (events & 0x0001U) : 0;
}
static uint16_t vpoll_pipe_write(uint64_t fd,uint16_t events) {
    return nas_pipe_poll_writable(file_slot[fd]) ? (events & 0x0004U) : 0;
}
static void varmpipe_read(struct nas_task *t,uint64_t fd,uint16_t events) {
    if(events & 0x0001U)nas_slot_mask_set(&t->poll_pipe_read,file_slot[fd]);
}
static void varmpipe_write(struct nas_task *t,uint64_t fd,uint16_t events) {
    if(events & 0x0004U)nas_slot_mask_set(&t->poll_pipe_write,file_slot[fd]);
}
static void varmtty(struct nas_task *t,uint64_t fd,uint16_t events) {
    (void)fd;
    if((events & 0x0001U) && nas_tty_read_waitable())t->poll_tty_read=true;
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
static void vrelease_tmp(unsigned slot){tmp_release_slot(slot);}
static const struct nas_file_ops ops_uart={
    .read=vread_tty,.write=vwrite_tty,.poll=vpoll_tty,
    .poll_arm=varmtty,.ioctl=vioctl_uart
};
static const struct nas_file_ops ops_tty={
    .read=vread_tty,.write=vwrite_tty,.poll=vpoll_tty,
    .poll_arm=varmtty,.ioctl=vioctl_tty
};
static const struct nas_file_ops ops_null={
    .read=vread_null,.write=vwrite_discard,.poll=vpoll_always
};
static const struct nas_file_ops ops_zero={
    .read=vread_zero,.write=vwrite_discard,.poll=vpoll_always
};
static const struct nas_file_ops ops_bootfs={.read=vread_limine,.poll=vpoll_always};
static const struct nas_file_ops ops_archive={.read=vread_archive,.poll=vpoll_always};
static const struct nas_file_ops ops_dir={.read=vread_dir,.poll=vpoll_always};
static const struct nas_file_ops ops_pipe_read={
    .read=pipe_fd_read,.poll=vpoll_pipe_read,
    .poll_arm=varmpipe_read,.release=vrelease_pipe
};
static const struct nas_file_ops ops_pipe_write={
    .write=pipe_fd_write,.poll=vpoll_pipe_write,
    .poll_arm=varmpipe_write,.release=vrelease_pipe
};
static uint16_t vpoll_socket(uint64_t fd,uint16_t events) {
    if(nas_unix_socket_listening(file_slot[fd]))
        return nas_unix_socket_readable(file_slot[fd]) ? (events&1U) : 0;
    return vpoll_pipe_read(fd,events) | vpoll_pipe_write(fd,events);
}
static void varm_socket(struct nas_task *t,uint64_t fd,uint16_t events) {
    varmpipe_read(t,fd,events);
    varmpipe_write(t,fd,events);
}
static const struct nas_file_ops ops_socket={
    .read=pipe_fd_read,.write=pipe_fd_write,.poll=vpoll_socket,
    .poll_arm=varm_socket,.release=vrelease_socket
};
static uint16_t vpoll_pty(uint64_t fd,uint16_t events) {
    return nas_pty_poll(file_slot[fd],events);
}
static void varm_pty(struct nas_task *task,uint64_t fd,uint16_t events) {
    if(events&1U)nas_slot_mask_set(&task->poll_pipe_read,file_slot[fd]);
    if(events&4U)nas_slot_mask_set(&task->poll_pipe_write,file_slot[fd]);
}
static void vrelease_pty(unsigned slot){nas_pty_release(slot);}
static const struct nas_file_ops ops_pty={
    .read=nas_pty_read,.write=nas_pty_write,.poll=vpoll_pty,
    .poll_arm=varm_pty,.ioctl=nas_pty_ioctl,.release=vrelease_pty
};
static const struct nas_file_ops ops_tmp={
    .read=tmp_fd_read,.write=tmp_fd_write,.poll=vpoll_always,
    .release=vrelease_tmp
};
static const struct nas_file_ops ops_tmp_dir={
    .read=vread_dir,.poll=vpoll_always,.release=vrelease_tmp
};
static uint16_t vpoll_epoll(uint64_t fd,uint16_t events){
    return nas_epoll_fd_poll(fd,events);
}
static void vrelease_epoll(unsigned slot){nas_epoll_release(slot);}
static void vrelease_eventfd(unsigned slot){nas_eventfd_release(slot);}
static const struct nas_file_ops ops_epoll={
    .poll=vpoll_epoll,.release=vrelease_epoll
};
static const struct nas_file_ops ops_eventfd={
    .read=nas_eventfd_read,.write=nas_eventfd_write,
    .poll=nas_eventfd_poll,.release=vrelease_eventfd
};
static const struct nas_file_ops *const ops_by_kind[]={
    [FD_UART]=&ops_uart,[FD_TTY]=&ops_tty,[FD_NULL]=&ops_null,
    [FD_ZERO]=&ops_zero,[FD_BOOTFS]=&ops_bootfs,[FD_ARCHIVE]=&ops_archive,
    [FD_DIR]=&ops_dir,[FD_PIPE_R]=&ops_pipe_read,[FD_PIPE_W]=&ops_pipe_write,
    [FD_TMP]=&ops_tmp,[FD_TMP_DIR]=&ops_tmp_dir,
    [FD_EPOLL]=&ops_epoll,
    [FD_EVENTFD]=&ops_eventfd,[FD_SOCKET]=&ops_socket,
    [FD_PTY_MASTER]=&ops_pty,[FD_PTY_SLAVE]=&ops_pty
};
const struct nas_file_ops *nas_fs_ops_for_kind(uint8_t kind) {
    return kind < sizeof(ops_by_kind)/sizeof(ops_by_kind[0])
           ? ops_by_kind[kind] : 0;
}

uint64_t nas_fs_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED)return (uint64_t)-EBADF;
    if((nas_open_files[file_slot[fd]].status_flags & (NAS_LINUX_O_PATH|3ULL))==0)
        return (uint64_t)-EBADF;
    /* Linux bounds one regular write to MAX_RW_COUNT (INT_MAX rounded down
     * to the page size), not a kernel-private 1-MiB limit. GNU binutils
     * legitimately calls writev() with a single multi-megabyte iovec.
     * The file/device ops still copy in bounded chunks and may short-write. */
    if(length>0x7ffff000ULL)length=0x7ffff000ULL;
    if(address>=USER_LIMIT || length>USER_LIMIT-address)
        return (uint64_t)-EFAULT;
    const struct nas_file_ops *ops=nas_open_files[file_slot[fd]].ops;
    return ops && ops->write ? ops->write(fd,address,length) : (uint64_t)-EBADF;
}
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED)return (uint64_t)-EBADF;
    if((nas_open_files[file_slot[fd]].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[file_slot[fd]].status_flags&3ULL)==1)
        return (uint64_t)-EBADF;
    const struct nas_file_ops *ops=nas_open_files[file_slot[fd]].ops;
    return ops && ops->read ? ops->read(fd,address,length) : (uint64_t)-EBADF;
}
/* The Linux positional I/O ABI is the same common VFS operation on all
 * three architectures. Respect per-FD access mode and reject pipes,
 * devices and directories with ESPIPE/EISDIR rather than pretending to
 * read a regular file. Keep offset independent of fork/dup shared state.
 * The same kernel-buffer regular-file reader already powers mmap. */
uint64_t nas_fs_fd_pread(uint64_t fd,uint64_t address,uint64_t count,
                         uint64_t offset) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED) return (uint64_t)-EBADF;
    unsigned slot=file_slot[fd];
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount) return (uint64_t)-EBADF;
    if((nas_open_files[slot].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[slot].status_flags&3ULL)==1ULL) return (uint64_t)-EBADF;
    if(descriptors[fd]==FD_DIR || descriptors[fd]==FD_TMP_DIR) return (uint64_t)-EISDIR;
    if(descriptors[fd]!=FD_TMP && descriptors[fd]!=FD_ARCHIVE &&
       descriptors[fd]!=FD_BOOTFS) return (uint64_t)-ESPIPE;
    if(offset>INT64_MAX || count>INT64_MAX-offset) return (uint64_t)-EINVAL;
    if(count && (address>=USER_LIMIT || count>USER_LIMIT-address))
        return (uint64_t)-EFAULT;
    uint8_t buffer[1024];
    uint64_t done=0;
    while(done<count) {
        uint64_t take=count-done;
        if(take>sizeof(buffer))take=sizeof(buffer);
        int64_t got=nas_fs_mmap_read(fd,offset+done,buffer,take);
        if(got<0)return done ? done : (uint64_t)got;
        if(!got)break;
        if(!nas_user_write_bytes(address+done,buffer,(uint64_t)got))
            return done ? done : (uint64_t)-EFAULT;
        done+=(uint64_t)got;
        if((uint64_t)got<take)break;
    }
    return done;
}
uint64_t nas_fs_fd_pwrite(uint64_t fd,uint64_t address,uint64_t count,
                          uint64_t offset) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED) return (uint64_t)-EBADF;
    unsigned slot=file_slot[fd];
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount) return (uint64_t)-EBADF;
    if((nas_open_files[slot].status_flags&NAS_LINUX_O_PATH) ||
       (nas_open_files[slot].status_flags&3ULL)==0ULL) return (uint64_t)-EBADF;
    if(descriptors[fd]==FD_DIR || descriptors[fd]==FD_TMP_DIR) return (uint64_t)-EISDIR;
    if(descriptors[fd]!=FD_TMP)return (uint64_t)-ESPIPE;
    if(offset>INT64_MAX || count>INT64_MAX-offset)return (uint64_t)-EINVAL;
    if(count && (address>=USER_LIMIT || count>USER_LIMIT-address))
        return (uint64_t)-EFAULT;
    struct nas_tmp_file *file=tmp_by_slot(slot);
    if(!file)return (uint64_t)-EBADF;
    uint64_t done=0;
    uint8_t buffer[1024];
    while(done<count) {
        uint64_t n=count-done;
        if(n>sizeof(buffer))n=sizeof(buffer);
        if(!nas_user_read_bytes(address+done,buffer,n))
            return done ? done : (uint64_t)-EFAULT;
        int64_t wrote=tmp_write_at(file,offset+done,buffer,n);
        if(wrote<0)return done ? done : (uint64_t)wrote;
        done+=(uint64_t)wrote;
        if((uint64_t)wrote<n)break;
    }
    return done;
}
/* Linux readv LP64 iovec. All slices share the open description's offset.
 * A partial transfer takes priority over an error. For an empty pipe the
 * standard internal wait sentinel returns without touching any buffer;
 * the blocked syscall is replayed only in its owner's user VM. */
uint64_t nas_fs_fd_readv(uint64_t fd,uint64_t iov,uint64_t count) {
    if (count > 64) return (uint64_t)-EINVAL;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT-iov)/16))
        return (uint64_t)-EFAULT;
    uint64_t total=0;
    for (uint64_t n=0;n<count;n++) {
        uint64_t address,bytes;
        if (!nas_user_read_u64(iov+n*16,&address) ||
            !nas_user_read_u64(iov+n*16+8,&bytes))
            return total ? total : (uint64_t)-EFAULT;
        if (bytes>INT64_MAX-total) return (uint64_t)-EINVAL;
        if (!bytes) continue;
        if (address>=USER_LIMIT || bytes>USER_LIMIT-address)
            return total ? total : (uint64_t)-EFAULT;
        uint64_t received=nas_fs_fd_read(fd,address,bytes);
        if ((int64_t)received<0) return total ? total : received;
        total+=received;
        if (received!=bytes) break;
    }
    return total;
}
uint64_t nas_fs_fd_writev(uint64_t fd, uint64_t iov, uint64_t count) {
    if (count > 64) return (uint64_t)-EINVAL;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT - iov) / 16))
        return (uint64_t)-EFAULT;
    uint64_t sent = 0;
    for (uint64_t n = 0; n < count; n++) {
        uint64_t address, bytes;
        if (!nas_user_read_u64(iov + n * 16, &address) ||
            !nas_user_read_u64(iov + n * 16 + 8, &bytes))
            return sent ? sent : (uint64_t)-EFAULT;
        /* The Linux total transfer limit applies across the entire iovec,
         * and the shared write path enforces pointer/FD validity. */
        uint64_t allowed = bytes;
        if (allowed > 0x7ffff000ULL - sent)
            allowed = 0x7ffff000ULL - sent;
        if (!allowed) break;
        uint64_t r = nas_fs_fd_write(fd, address, allowed);
        if ((int64_t)r < 0) return sent ? sent : r;
        sent += r;
        if (r != allowed || allowed != bytes) break;
    }
    return sent;
}
/* Kernel Linux pollfd ABI: int32 fd, int16 events, int16 revents.
 * The only supported blocking timeout is infinite; positive timeouts need
 * monotonic deadline ownership is provided by the Task scheduler.
 * Readiness and the snapshot are distinct: pollfd copy-out is in the
 * owning task's VM, scheduler tests only a kernel-owned interest snapshot.
 */
uint64_t nas_fs_poll(uint64_t address, uint64_t nfds, uint64_t timeout) {
    const uint16_t POLLNVAL = 0x0020;
    if (nfds > 64) return (uint64_t)-EINVAL;
    if (nfds && (address >= USER_LIMIT || nfds > (USER_LIMIT - address) / 8))
        return (uint64_t)-EFAULT;
    uint64_t ready = 0;
    for (uint64_t i = 0; i < nfds; ++i) {
        uint64_t word;
        if (!nas_user_read_u64(address + i * 8, &word)) return (uint64_t)-EFAULT;
        int32_t fd = (int32_t)(uint32_t)word;
        uint16_t events = (uint16_t)(word >> 32);
        uint16_t revents = 0;
        if (fd >= 0) {
            if (fd >= 64 || descriptors[(unsigned)fd] == FD_CLOSED)
                revents = POLLNVAL;
            else {
                const struct nas_file_ops *ops=nas_open_files[file_slot[(unsigned)fd]].ops;
                revents=ops && ops->poll ? ops->poll((uint64_t)(unsigned)fd,events) : 0;
            }
        }
        if (revents) ready++;
        word = (word & 0x0000ffffffffffffULL) | ((uint64_t)revents << 48);
        if (!nas_user_write_u64(address + i * 8, word)) return (uint64_t)-EFAULT;
    }
    int32_t ms=(int32_t)(uint32_t)timeout;
    if(ready || ms==0)return ready;
    /* Deadline belongs to the current Task and survives syscall replay;
     * zero return is reached only after the hardware clock truly advances. */
    if(TASK.deadline_active &&
       nas_time_deadline_expired(nas_arch_monotonic_ns(),TASK.deadline_ns))
        return 0;
    return (uint64_t)-EAGAIN; /* dispatcher arms actual I/O or timer wait */
}
/* Linux asm-generic supplies ppoll(73), not legacy poll(7).
 * A NULL timeout means infinity, zero timespec means immediate. A finite
 * timeout is converted once by the dispatcher to an absolute Task deadline.
 * Atomic temporary sigmask remains unsupported until signal frames exist. */
uint64_t nas_fs_ppoll(uint64_t addr,uint64_t nfds,
                      uint64_t timeout_ptr,uint64_t sigmask_ptr) {
    /* Temporary sigmask belongs to dispatcher/scheduler, not FS I/O. */
    (void)sigmask_ptr;
    if(!timeout_ptr)return nas_fs_poll(addr,nfds,(uint64_t)-1);
    uint64_t secs,nsecs;
    if(!nas_user_read_u64(timeout_ptr,&secs) ||
       timeout_ptr>UINT64_MAX-8 ||
       !nas_user_read_u64(timeout_ptr+8,&nsecs))
        return (uint64_t)-EFAULT;
    uint64_t ns;
    if(!nas_time_duration_ns(secs,nsecs,&ns))return (uint64_t)-EINVAL;
    return nas_fs_poll(addr,nfds,ns ? (uint64_t)-1 : 0);
}
/* Native Linux pselect6 over the EXISTING file-ops poll providers.
 * Like ppoll, only the owning Task touches userspace. No foreign address
 * space is consulted while another task's pipe/jobserver wakes up.
 * fd_set is an array of native unsigned-long words; our bounded 64-FD
 * implementation validates and transfers exactly one 64-bit word.
 * Input interests are preserved on EAGAIN for syscall-boundary replay. */
static bool select_read_bits(uint64_t ptr,uint64_t *mask) {
    *mask=0;
    return !ptr || nas_user_read_u64(ptr,mask);
}
uint64_t nas_fs_pselect6(uint64_t nfds,uint64_t readfds,
                         uint64_t writefds,uint64_t exceptfds,
                         bool immediate) {
    if(nfds>NAS_FD_LIMIT)return (uint64_t)-EINVAL;
    uint64_t readset,writeset,exceptset;
    if(!select_read_bits(readfds,&readset) ||
       !select_read_bits(writefds,&writeset) ||
       !select_read_bits(exceptfds,&exceptset))
        return (uint64_t)-EFAULT;
    const uint64_t limit=nfds==64?UINT64_MAX:((1ULL<<nfds)-1);
    readset &= limit; writeset &= limit; exceptset &= limit;
    uint64_t readready=0,writeready=0,exceptready=0;
    uint64_t count=0;
    for(unsigned fd=0;fd<nfds;fd++) {
        uint64_t bit=1ULL<<fd;
        uint16_t events=0;
        if(readset&bit)events|=1U;      /* POLLIN */
        if(writeset&bit)events|=4U;     /* POLLOUT */
        if(exceptset&bit)events|=2U;    /* POLLPRI */
        if(!events)continue;
        if(descriptors[fd]==FD_CLOSED)return (uint64_t)-EBADF;
        const struct nas_file_ops *ops=nas_open_files[file_slot[fd]].ops;
        uint16_t revents=ops && ops->poll ? ops->poll(fd,events) : 0;
        bool any=false;
        if((readset&bit) && (revents&(1U|8U|16U))) {
            readready|=bit;any=true;
        }
        if((writeset&bit) && (revents&(4U|8U|16U))) {
            writeready|=bit;any=true;
        }
        if((exceptset&bit) && (revents&2U)) {
            exceptready|=bit;any=true;
        }
        if(any)count++;
    }
    if(!count && !immediate &&
       !(TASK.deadline_active &&
         nas_time_deadline_expired(nas_arch_monotonic_ns(),TASK.deadline_ns)))
        return (uint64_t)-EAGAIN;
    if((readfds && !nas_user_write_u64(readfds,readready)) ||
       (writefds && !nas_user_write_u64(writefds,writeready)) ||
       (exceptfds && !nas_user_write_u64(exceptfds,exceptready)))
        return (uint64_t)-EFAULT;
    return count;
}
bool nas_fs_pselect_arm(uint64_t nfds,uint64_t readfds,
                        uint64_t writefds,uint64_t exceptfds) {
    struct nas_task *task=nas_current_task;
    nas_slot_mask_clear(&task->poll_pipe_read);
    nas_slot_mask_clear(&task->poll_pipe_write);
    task->poll_tty_read=false;
    uint64_t readset,writeset,exceptset;
    if(nfds>NAS_FD_LIMIT ||
       !select_read_bits(readfds,&readset) ||
       !select_read_bits(writefds,&writeset) ||
       !select_read_bits(exceptfds,&exceptset))return false;
    for(unsigned fd=0;fd<nfds;fd++) {
        uint64_t bit=1ULL<<fd;
        uint16_t events=0;
        if(readset&bit)events|=1U;
        if(writeset&bit)events|=4U;
        if(exceptset&bit)events|=2U;
        if(!events || descriptors[fd]==FD_CLOSED)continue;
        unsigned slot=file_slot[fd];
        if(slot>=NAS_OPEN_FILE_LIMIT)continue;
        const struct nas_file_ops *ops=nas_open_files[slot].ops;
        if(ops && ops->poll_arm)ops->poll_arm(task,fd,events);
    }
    return task->poll_tty_read ||
        nas_slot_mask_any(&task->poll_pipe_read) ||
        nas_slot_mask_any(&task->poll_pipe_write);
}
bool nas_fs_poll_arm(uint64_t address,uint64_t nfds) {
    struct nas_task *task=nas_current_task;
    nas_slot_mask_clear(&task->poll_pipe_read);
    nas_slot_mask_clear(&task->poll_pipe_write);
    task->poll_tty_read=false;
    if(nfds>NAS_FD_LIMIT)return false; /* nas_fs_poll checked bounds */
    for(uint64_t i=0;i<nfds;i++) {
        uint64_t raw;
        if(!nas_user_read_u64(address+i*8,&raw))return false;
        int32_t fd=(int32_t)(uint32_t)raw;
        uint16_t events=(uint16_t)(raw>>32);
        if(fd<0 || fd>=NAS_FD_LIMIT || !events)continue;
        unsigned slot=file_slot[(unsigned)fd];
        if(slot>=NAS_OPEN_FILE_LIMIT)continue;
        const struct nas_file_ops *ops=nas_open_files[slot].ops;
        if(ops && ops->poll_arm)ops->poll_arm(task,(uint64_t)(unsigned)fd,events);
    }
    return task->poll_tty_read ||
        nas_slot_mask_any(&task->poll_pipe_read) ||
        nas_slot_mask_any(&task->poll_pipe_write);
}
bool nas_fs_poll_ready(const struct nas_task *task) {
    if(task->poll_tty_read && nas_tty_poll_readable())return true;
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        if(nas_slot_mask_test(&task->poll_pipe_read,slot) &&
           (nas_pipe_poll_readable(slot) || nas_unix_socket_readable(slot) ||
            (nas_pty_number(slot)<NAS_PTY_LIMIT && nas_pty_read_ready(slot))))
            return true;
        if(nas_slot_mask_test(&task->poll_pipe_write,slot) &&
           (nas_pipe_poll_writable(slot) ||
            (nas_pty_number(slot)<NAS_PTY_LIMIT && nas_pty_write_ready(slot))))
            return true;
    }
    return false;
}

static uint64_t sys_getcwd_root(uint64_t buffer, uint64_t size) {
    uint64_t length = 0;
    while (current_workdir[length]) length++;
    length++; /* Linux raw getcwd includes NUL in returned length. */
    if (size < length) return (uint64_t)-ERANGE;
    return nas_user_write_bytes(buffer, (const uint8_t *)current_workdir, length)
        ? length : (uint64_t)-EFAULT;
}
static uint64_t sys_chdir(uint64_t pathname) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path((uint64_t)-100, pathname, path);
    if ((int64_t)ret < 0) return ret;
    char actual[256];
    int err = nas_fs_resolve_existing(path, true, actual);
    if (err) {
        struct nas_tmp_file *t = tmp_find(path);
        if (!t) return (uint64_t)(int64_t)err;
        if (!t->is_dir) return (uint64_t)-ENOTDIR;
        unsigned n = 0;
        do { current_workdir[n] = path[n]; } while (path[n++]);
        return 0;
    }
    const struct nas_tmp_file *tmp=tmp_find(actual);
    if (tmp) {
        if (!tmp->is_dir) return (uint64_t)-ENOTDIR;
        unsigned j=0;
        do { current_workdir[j]=actual[j]; } while (actual[j++]);
        return 0;
    }
    const struct bootfs_entry *entry = bootfs_find(actual);
    if (!bootfs_is_dir(entry)) return (uint64_t)-ENOTDIR;
    unsigned i = 0;
    do { current_workdir[i] = actual[i]; } while (actual[i++]);
    return 0;
}
static uint64_t sys_fchdir(uint64_t fd) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED)
        return (uint64_t)-EBADF;
    const char *path;
    if (descriptors[fd] == FD_TMP_DIR) {
        const struct nas_tmp_file *dir = tmp_by_fd(fd);
        if (!dir || !dir->linked) return (uint64_t)-ENOENT;
        path = dir->path;
    } else {
        if (descriptors[fd] != FD_DIR || !nas_open_files[file_slot[fd]].boot_entry)
            return (uint64_t)-ENOTDIR;
        path = nas_open_files[file_slot[fd]].boot_entry->path;
    }
    unsigned i = 0;
    do { current_workdir[i] = path[i]; } while (path[i++]);
    return 0;
}

/* Same inode metadata for both fstat() and path-based stat/lstat; critical
 * for a symlink's S_IFLNK type, target length, and nofollow distinction. */
static void fill_tmp_stat(struct nas_linux_stat *st,
                          const struct nas_tmp_file *file) {
    st->dev = 5; st->ino = 5000 + tmp_number(file);
    st->mode = file->mode;
    st->nlink = file->linked ? (file->is_dir ? 2 : 1) : 0;
    st->size = (int64_t)file->size;
    st->blocks = (int64_t)((file->size + 511) / 512);
    st->atime_sec = (int64_t)(file->atime_ns / NAS_NSEC_PER_SEC);
    st->atime_nsec = file->atime_ns % NAS_NSEC_PER_SEC;
    st->mtime_sec = (int64_t)(file->mtime_ns / NAS_NSEC_PER_SEC);
    st->mtime_nsec = file->mtime_ns % NAS_NSEC_PER_SEC;
    st->ctime_sec = (int64_t)(file->ctime_ns / NAS_NSEC_PER_SEC);
    st->ctime_nsec = file->ctime_ns % NAS_NSEC_PER_SEC;
}
/* Linux x86-64 struct stat layout, 144 bytes. File size is actual module.
 * Device entries have character-device type; mounted files are regular.
 */
static uint64_t sys_fstat_boot(uint64_t fd, uint64_t address) {
    if (fd >= 64 || !descriptors[fd]) return (uint64_t)-EBADF;
    struct nas_linux_stat st;
    volatile uint8_t *bytes = (volatile uint8_t *)&st;
    for (unsigned i = 0; i < sizeof(st); i++) bytes[i] = 0;
    st.nlink = 1;
    st.blksize = PAGE;
    if (descriptors[fd] == FD_DIR || descriptors[fd] == FD_ARCHIVE) {
        const struct bootfs_entry *entry = nas_open_files[file_slot[fd]].boot_entry;
        if (!entry) return (uint64_t)-EBADF;
        st.dev = 3; st.ino = entry->ino;
        st.mode = entry->mode;
        st.nlink = bootfs_is_dir(entry) ? 2 : entry->nlink;
        st.size = (int64_t)entry->size;
        st.blocks = (int64_t)((entry->size + 511) / 512);
        st.atime_sec = st.mtime_sec = st.ctime_sec = (int64_t)entry->mtime_sec;
    } else if (descriptors[fd] == FD_TMP || descriptors[fd] == FD_TMP_DIR) {
        const struct nas_tmp_file *file = tmp_by_fd(fd);
        if (!file) return (uint64_t)-EBADF;
        fill_tmp_stat(&st,file);
    } else if (descriptors[fd] == FD_PTY_MASTER ||
               descriptors[fd] == FD_PTY_SLAVE) {
        st.dev=6;st.ino=2000+nas_pty_number(file_slot[fd]);
        st.mode=0020620; /* real PTY character device */
    } else if (descriptors[fd] == FD_PIPE_R || descriptors[fd] == FD_PIPE_W ||
               descriptors[fd] == FD_SOCKET) {
        st.dev = 4; st.ino = (uint64_t)file_slot[fd] + 1000;
        st.mode = descriptors[fd] == FD_SOCKET ? 0140777 : 0010600;
        /* Linux presents socketpair as S_IFSOCK, pipe as S_IFIFO. */
    } else if (descriptors[fd] == FD_BOOTFS) {
        const struct limine_file *file = nas_open_files[file_slot[fd]].boot_file;
        if (!file) return (uint64_t)-EBADF;
        st.dev = 1; st.ino = fd + 100;
        st.mode = 0100555;
        st.size = (int64_t)file->size;
        st.blocks = (int64_t)((file->size + 511) / 512);
    } else {
        st.dev = 2; st.ino = fd + 10;
        st.mode = 0020666; /* real character device backed by UART/null/zero */
    }
    if (!nas_user_write_bytes(address, (const uint8_t *)&st, sizeof(st)))
        return (uint64_t)-EFAULT;
    return 0;
}
/* In a volatile tmpfs there is no asynchronous durable writeback.
 * fsync/fdatasync return success only for actual open RAM inodes. */
static uint64_t sys_sync_fd(uint64_t fd) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[file_slot[fd]].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    if(descriptors[fd]!=FD_TMP && descriptors[fd]!=FD_TMP_DIR)
        return (uint64_t)-EINVAL;
    return tmp_by_fd(fd) ? 0 : (uint64_t)-EBADF;
}
static uint64_t sys_resize_fd(uint64_t fd,uint64_t length) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED)return (uint64_t)-EBADF;
    if(nas_open_files[file_slot[fd]].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    if(descriptors[fd]==FD_ARCHIVE || descriptors[fd]==FD_BOOTFS)
        return (uint64_t)-EROFS;
    if(descriptors[fd]!=FD_TMP)return (uint64_t)-EINVAL;
    if((nas_open_files[file_slot[fd]].status_flags&3ULL)==0)
        return (uint64_t)-EINVAL;
    return (uint64_t)(int64_t)tmp_resize(tmp_by_fd(fd),length);
}
static uint64_t sys_resize_path(uint64_t pathname,uint64_t length) {
    char path[256];
    uint64_t ret=nas_fs_resolve_path((uint64_t)-100,pathname,path);
    if((int64_t)ret<0)return ret;
    struct nas_tmp_file *file=tmp_find(path);
    if(file)return (uint64_t)(int64_t)tmp_resize(file,length);
    return bootfs_find(path) ? (uint64_t)-EROFS : (uint64_t)-ENOENT;
}
static uint64_t sys_lseek_boot(uint64_t fd, uint64_t off, uint64_t whence) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED)
        return (uint64_t)-EBADF;
    if(nas_open_files[file_slot[fd]].status_flags&NAS_LINUX_O_PATH)
        return (uint64_t)-EBADF;
    uint64_t size = 0;
    if (descriptors[fd] == FD_BOOTFS && nas_open_files[file_slot[fd]].boot_file)
        size = nas_open_files[file_slot[fd]].boot_file->size;
    else if (descriptors[fd] == FD_ARCHIVE && nas_open_files[file_slot[fd]].boot_entry)
        size = nas_open_files[file_slot[fd]].boot_entry->size;
    else if (descriptors[fd] == FD_TMP || descriptors[fd] == FD_TMP_DIR) {
        const struct nas_tmp_file *file = tmp_by_fd(fd);
        if (!file) return (uint64_t)-EBADF;
        size = file->size;
    }
    else if (descriptors[fd] != FD_DIR || !nas_open_files[file_slot[fd]].boot_entry)
        return (uint64_t)-ESPIPE;
    uint64_t base = 0;
    if (whence == 1) base = nas_open_files[file_slot[fd]].position;
    else if (whence == 2 && descriptors[fd] != FD_DIR && descriptors[fd] != FD_TMP_DIR) base = size;
    else if (whence != 0) return (uint64_t)-EINVAL;
    int64_t signed_off = (int64_t)off;
    if (signed_off < 0) {
        uint64_t magnitude = 0 - off;
        if (magnitude > base) return (uint64_t)-EINVAL;
        base -= magnitude;
    } else {
        if (base > UINT64_MAX - off) return (uint64_t)-EINVAL;
        base += off;
    }
    nas_open_files[file_slot[fd]].position = base;
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
    char path[256], actual[256];
    uint64_t rc=nas_fs_resolve_path(dirfd,pathname,path);
    if((int64_t)rc<0) return rc;
    uint32_t bits=0;
    bool writable=false;
    bool is_dir=false;
    struct nas_fs_node node;
    int err=nas_fs_lookup_node(path,!(flags&AT_SYMLINK_NOFOLLOW),
                               &node,actual);
    if(!err) {
        bits=node.mode;
        is_dir=node.is_dir;
        writable=node.backend==NAS_FS_TMPFS;
    } else if(err!=-ENOENT)return (uint64_t)(int64_t)err;
    if(!bits && (!strcmp(path,"/dev/ptmx") ||
                 (nas_pty_path_number(path)>=0 &&
                  nas_pty_present((unsigned)nas_pty_path_number(path))) ||
                 !strcmp(path,"/dev/null") || !strcmp(path,"/dev/zero") ||
                 !strcmp(path,"/dev/console") || !strcmp(path,"/dev/ttyS0") ||
                 !strcmp(path,"/dev/tty"))) {
        if(!strcmp(path,"/dev/tty") && !nas_tty_controlling() &&
           !nas_pty_controlling())
            return (uint64_t)-ENXIO;
        bits=0020666U; writable=true;
    }
    if(!bits)return (uint64_t)-ENOENT;
    if((mode & 1ULL) && !(bits & 0111U))return (uint64_t)-EACCES;
    if((mode & 2ULL) && !writable)return (uint64_t)-EROFS;
    if((mode & 2ULL) && !is_dir && (bits&0170000U)==0040000U)
        return (uint64_t)-EACCES;
    return 0;
}
/* chmod operates on tmpfs inode identity (not FD pathname copies).
 * Linux ABI/path and dirfd decoding stay in this syscall boundary. */
static uint64_t sys_fchmod_fd(uint64_t fd,uint64_t mode) {
    if(fd>=NAS_FD_LIMIT || descriptors[fd]==FD_CLOSED)
        return (uint64_t)-EBADF;
    if(descriptors[fd]!=FD_TMP && descriptors[fd]!=FD_TMP_DIR)
        return (uint64_t)-EROFS;
    return (uint64_t)(int64_t)tmp_chmod((struct nas_tmp_file*)tmp_by_fd(fd),mode);
}
static uint64_t sys_chmod_path(uint64_t dirfd,uint64_t filename,
                               uint64_t mode) {
    char path[256], actual[256];
    uint64_t ret=nas_fs_resolve_path(dirfd,filename,path);
    if((int64_t)ret<0)return ret;
    struct nas_fs_node node;
    int err=nas_fs_lookup_node(path,true,&node,actual);
    if(err)return (uint64_t)(int64_t)err;
    if(node.backend!=NAS_FS_TMPFS)return (uint64_t)-EROFS;
    return (uint64_t)(int64_t)tmp_chmod(tmp_find(actual),mode);
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
    struct nas_tmp_file *file=0;
    if (!pathname) {
        if (dirfd >= NAS_FD_LIMIT || descriptors[dirfd] == FD_CLOSED)
            return (uint64_t)-EBADF;
        if (descriptors[dirfd] != FD_TMP && descriptors[dirfd] != FD_TMP_DIR)
            return (uint64_t)-EROFS;
        file=tmp_by_slot(file_slot[dirfd]);
    } else {
        char path[256], actual[256];
        uint64_t ret=nas_fs_resolve_path(dirfd,pathname,path);
        if ((int64_t)ret<0) return ret;
        struct nas_fs_node node;
        int err=nas_fs_lookup_node(path,!(flags&AT_SYMLINK_NOFOLLOW),
                                   &node,actual);
        if (err) return (uint64_t)(int64_t)err;
        if (node.backend!=NAS_FS_TMPFS) return (uint64_t)-EROFS;
        file=(struct nas_tmp_file *)(uintptr_t)node.tmp;
    }
    if (!file) return (uint64_t)-ENOENT;
    uint64_t now=0;
    if (!nas_time_realtime_ns(&now)) now=nas_arch_monotonic_ns();
    uint64_t ns[2]={now,now};
    bool set[2]={true,true};
    if (timesptr) {
        int64_t raw[4];
        if (!nas_user_read_bytes(timesptr,(uint8_t *)raw,sizeof(raw)))
            return (uint64_t)-EFAULT;
        for (unsigned i=0;i<2;i++) {
            uint64_t nsec=(uint64_t)raw[i*2+1];
            if (nsec==UTIME_OMIT) { set[i]=false; continue; }
            if (nsec==UTIME_NOW) continue;
            if (raw[i*2]<0 || raw[i*2]>INT64_MAX/(int64_t)NAS_NSEC_PER_SEC ||
                raw[i*2+1]<0 || nsec>=NAS_NSEC_PER_SEC)
                return (uint64_t)-EINVAL;
            ns[i]=(uint64_t)raw[i*2]*NAS_NSEC_PER_SEC+nsec;
        }
    }
    return (uint64_t)(int64_t)tmp_utimens(file,ns[0],ns[1],set[0],set[1]);
}
static uint64_t sys_openat_boot(uint64_t dirfd, uint64_t pathname,
                                uint64_t flags,uint64_t mode) {
    char path[256];
    uint64_t result = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)result < 0 ? result : sys_open_path(path,flags,mode);
}
static uint64_t write_boot_stat(const struct bootfs_entry *entry, uint64_t dst) {
    struct nas_linux_stat st;
    uint8_t *p = (uint8_t *)&st;
    for (unsigned i = 0; i < sizeof(st); i++) p[i] = 0;
    st.dev = 3;
    st.ino = entry->ino;
    st.nlink = bootfs_is_dir(entry) ? 2 : entry->nlink;
    st.mode = entry->mode;
    st.blksize = PAGE;
    st.size = (int64_t)entry->size;
    st.blocks = (int64_t)((entry->size + 511) / 512);
    st.atime_sec = st.mtime_sec = st.ctime_sec = (int64_t)entry->mtime_sec;
    return nas_user_write_bytes(dst, (const uint8_t *)&st, sizeof(st))
        ? 0 : (uint64_t)-EFAULT;
}
static uint64_t write_tmp_stat(const struct nas_tmp_file *file,uint64_t dst) {
    struct nas_linux_stat st={0};
    st.blksize=PAGE;
    fill_tmp_stat(&st,file);
    return nas_user_write_bytes(dst,(const uint8_t *)&st,sizeof(st)) ?
           0 : (uint64_t)-EFAULT;
}
static uint64_t sys_fstatat_boot(uint64_t dirfd, uint64_t pathname,
                                 uint64_t statbuf, uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW = 0x100ULL;
    const uint64_t AT_EMPTY_PATH = 0x1000ULL;
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    uint8_t first;
    if (!nas_user_read_bytes(pathname, &first, 1)) return (uint64_t)-EFAULT;
    if (first == 0 && (flags & AT_EMPTY_PATH))
        return sys_fstat_boot(dirfd, statbuf);
    char path[256];
    uint64_t result = nas_fs_resolve_path(dirfd, pathname, path);
    if ((int64_t)result < 0) return result;
    if(!strcmp(path,"/dev/ptmx") ||
       (nas_pty_path_number(path)>=0 &&
        nas_pty_present((unsigned)nas_pty_path_number(path)))) {
        struct nas_linux_stat device={0};
        device.dev=6;device.ino=2000;
        device.mode=0020620;device.nlink=1;device.blksize=PAGE;
        return nas_user_write_bytes(statbuf,(const uint8_t *)&device,sizeof(device))
            ? 0:(uint64_t)-EFAULT;
    }
    if (bootfs_mounted()) {
        char actual[256];
        int err = nas_fs_resolve_existing(path, !(flags & AT_SYMLINK_NOFOLLOW), actual);
        if (!err) {
            const struct nas_tmp_file *tmp=tmp_find(actual);
            if (tmp) return write_tmp_stat(tmp,statbuf);
            return write_boot_stat(bootfs_find(actual),statbuf);
        }
        if (err != -ENOENT) return (uint64_t)(int64_t)err;
    }
    /* Legacy modules and character devices retain their existing fstat. */
    result = sys_open_path(path, 0, 0);
    if ((int64_t)result < 0) return result;
    uint64_t ret = sys_fstat_boot(result, statbuf);
    nas_fs_close_fd(result);
    return ret;
}

/* Linux x86-64 linux_dirent64: ino64, off64, reclen16, dtype8, name[].
 * The offset cookie indexes '.', '..', then direct children of a directory.
 */
/* Linux readlink/readlinkat return the link bytes without a terminating NUL,
 * and deliberately do not follow the last component. */
static uint64_t sys_readlinkat_boot(uint64_t dirfd, uint64_t pathname,
                                     uint64_t dst, uint64_t capacity) {
    if (!capacity) return (uint64_t)-EINVAL;
    char path[256], actual[256];
    uint64_t r = nas_fs_resolve_path(dirfd, pathname, path);
    if ((int64_t)r < 0) return r;
    int err = nas_fs_resolve_existing(path, false, actual);
    if (err) return (uint64_t)(int64_t)err;
    const struct nas_tmp_file *tmp=tmp_find(actual);
    if (tmp) {
        if (!tmp_is_link(tmp)) return (uint64_t)-EINVAL;
        uint8_t content[256];
        int64_t got=tmp_read_at(tmp,0,content,
                     tmp->size<capacity?tmp->size:capacity);
        if (got<0) return (uint64_t)got;
        return nas_user_write_bytes(dst,content,(uint64_t)got) ?
               (uint64_t)got : (uint64_t)-EFAULT;
    }
    const struct bootfs_entry *entry = bootfs_find(actual);
    if (!bootfs_is_link(entry)) return (uint64_t)-EINVAL;
    uint64_t len = entry->size < capacity ? entry->size : capacity;
    return nas_user_write_bytes(dst, entry->data, len) ? len : (uint64_t)-EFAULT;
}

static uint64_t sys_getdents64(uint64_t fd, uint64_t dst, uint64_t capacity) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED) return (uint64_t)-EBADF;
    if (descriptors[fd] != FD_TMP_DIR &&
        (descriptors[fd] != FD_DIR || !nas_open_files[file_slot[fd]].boot_entry))
        return (uint64_t)-ENOTDIR;
    if (capacity < 24) return (uint64_t)-EINVAL;
    if (dst >= USER_LIMIT || capacity > USER_LIMIT - dst)
        return (uint64_t)-EFAULT;
    const struct bootfs_entry *dir = nas_open_files[file_slot[fd]].boot_entry;
    const struct nas_tmp_file *tmpdir =
        descriptors[fd] == FD_TMP_DIR ? tmp_by_fd(fd) : 0;
    const char *dirpath = tmpdir ? tmpdir->path : dir->path;
    uint64_t dirino = tmpdir ? 5000 + tmp_number(tmpdir) : dir->ino;
    uint64_t cursor = nas_open_files[file_slot[fd]].position;
    uint64_t written = 0;
    while (written + 24 <= capacity) {
        const struct bootfs_entry *item;
        const char *name;
        uint64_t ino;
        unsigned dtype;
        if (cursor == 0) {
            name = "."; ino = dirino; dtype = 4;
        } else if (cursor == 1) {
            name = ".."; dtype = 4;
            char parent[256];
            unsigned len = 0;
            while (dirpath[len]) { parent[len] = dirpath[len]; len++; }
            while (len > 1 && parent[len - 1] != '/') len--;
            if (len > 1) len--;
            parent[len] = 0;
            item = bootfs_find(parent);
            struct nas_tmp_file *tparent = tmp_find(parent);
            ino = tparent ? 5000 + tmp_number(tparent) :
                  item ? item->ino : dirino;
        } else if ((strcmp(dirpath, "/tmp") == 0) || tmpdir) {
            /* tmpfs uses lazy metadata slabs: end of directory is the
             * first missing child, never a global eight-inode bound. */
            if (cursor - 2 > UINT32_MAX) break;
            const struct nas_tmp_file *file =
                tmp_child(dirpath, (unsigned)(cursor - 2));
            if (!file) break;
            name = file->path;
            for (const char *p = file->path; *p; p++)
                if (*p == '/') name = p + 1;
            ino = 5000 + tmp_number(file);
            dtype = file->is_dir ? 4 : tmp_is_link(file) ? 10 :
                    (file->mode&0170000U)==0140000U ? 12 : 8;
        } else {
            if (cursor - 2 > UINT32_MAX) break;
            item = bootfs_child(dir, (unsigned)(cursor - 2));
            if (!item) break;
            name = item->path;
            for (const char *p = item->path; *p; p++)
                if (*p == '/') name = p + 1;
            ino = item->ino;
            dtype = bootfs_is_dir(item) ? 4 : bootfs_is_link(item) ? 10 : 8;
        }
        unsigned len = 0;
        while (name[len]) len++;
        unsigned reclen = (19 + len + 1 + 7) & ~7U;
        if (reclen > capacity - written) {
            if (!written) return (uint64_t)-EINVAL;
            break;
        }
        uint8_t record[280];
        for (unsigned i = 0; i < reclen; i++) record[i] = 0;
        for (unsigned i = 0; i < 8; i++) {
            record[i] = (uint8_t)(ino >> (i * 8));
            record[8 + i] = (uint8_t)((cursor + 1) >> (i * 8));
        }
        record[16] = (uint8_t)reclen;
        record[17] = (uint8_t)(reclen >> 8);
        record[18] = (uint8_t)dtype;
        for (unsigned i = 0; i < len; i++) record[19 + i] = (uint8_t)name[i];
        if (!nas_user_write_bytes(dst + written, record, reclen))
            return written ? written : (uint64_t)-EFAULT;
        written += reclen;
        cursor++;
        nas_open_files[file_slot[fd]].position = cursor;
    }
    return written;
}

/* A known filesystem operation returns its genuine Linux ABI result.
 * Non-FS operations remain owned by the common dispatcher. */
#define FS_RESULT(expr) return (struct nas_fs_result){ .handled = true, .value = (expr) }
struct nas_fs_result nas_fs_syscall(enum nas_syscall_op op,
                                     const uint64_t *args) {
    switch (op) {
    case NAS_SC_rename: FS_RESULT(sys_renameat_tmp((uint64_t)-100, args[1], (uint64_t)-100, args[2], 0));
    case NAS_SC_renameat: FS_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], 0));
    case NAS_SC_renameat2: FS_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], args[5]));
    case NAS_SC_symlink: FS_RESULT(sys_symlinkat_tmp(args[1], (uint64_t)-100, args[2]));
    case NAS_SC_symlinkat: FS_RESULT(sys_symlinkat_tmp(args[1], args[2], args[3]));
    case NAS_SC_mkdir: FS_RESULT(sys_mkdirat_tmp((uint64_t)-100, args[1], args[2]));
    case NAS_SC_mkdirat: FS_RESULT(sys_mkdirat_tmp(args[1], args[2], args[3]));
    case NAS_SC_unlink: FS_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0));
    case NAS_SC_rmdir: FS_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0x200ULL));
    case NAS_SC_unlinkat: FS_RESULT(sys_unlinkat_tmp(args[1], args[2], args[3]));
    case NAS_SC_chmod: FS_RESULT(sys_chmod_path((uint64_t)-100,args[1],args[2]));
    case NAS_SC_fchmod: FS_RESULT(sys_fchmod_fd(args[1],args[2]));
    case NAS_SC_fchmodat: FS_RESULT(sys_chmod_path(args[1],args[2],args[3]));
    case NAS_SC_utimensat: FS_RESULT(sys_utimensat_file(args[1],args[2],args[3],args[4]));
    case NAS_SC_access: FS_RESULT(sys_access_path((uint64_t)-100,args[1],args[2],0));
    case NAS_SC_faccessat: FS_RESULT(sys_access_path(args[1],args[2],args[3],0));
    case NAS_SC_faccessat2: FS_RESULT(sys_access_path(args[1],args[2],args[3],args[4]));
    case NAS_SC_open: FS_RESULT(sys_open_device(args[1],args[2],args[3]));
    case NAS_SC_openat: FS_RESULT(sys_openat_boot(args[1],args[2],args[3],args[4]));
    case NAS_SC_fstat: FS_RESULT(sys_fstat_boot(args[1], args[2]));
    case NAS_SC_newfstatat: FS_RESULT(sys_fstatat_boot(args[1], args[2], args[3], args[4]));
    case NAS_SC_stat: FS_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0));
    case NAS_SC_lstat: FS_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0x100ULL));
    case NAS_SC_lseek: FS_RESULT(sys_lseek_boot(args[1], args[2], args[3]));
    case NAS_SC_fsync: case NAS_SC_fdatasync: FS_RESULT(sys_sync_fd(args[1]));
    case NAS_SC_ftruncate: FS_RESULT(sys_resize_fd(args[1],args[2]));
    case NAS_SC_truncate: FS_RESULT(sys_resize_path(args[1],args[2]));
    case NAS_SC_pread64: FS_RESULT(nas_fs_fd_pread(args[1],args[2],args[3],args[4]));
    case NAS_SC_pwrite64: FS_RESULT(nas_fs_fd_pwrite(args[1],args[2],args[3],args[4]));
    case NAS_SC_getdents64: FS_RESULT(sys_getdents64(args[1], args[2], args[3]));
    case NAS_SC_readlink: FS_RESULT(sys_readlinkat_boot((uint64_t)-100, args[1], args[2], args[3]));
    case NAS_SC_readlinkat: FS_RESULT(sys_readlinkat_boot(args[1], args[2], args[3], args[4]));
    case NAS_SC_getcwd: FS_RESULT(sys_getcwd_root(args[1], args[2]));
    case NAS_SC_pipe: FS_RESULT(sys_pipe2(args[1], 0));
    case NAS_SC_pipe2: FS_RESULT(sys_pipe2(args[1], args[2]));
    case NAS_SC_socketpair: FS_RESULT(sys_socketpair(args[1],args[2],args[3],args[4]));
    case NAS_SC_socket: FS_RESULT(sys_socket(args[1],args[2],args[3]));
    case NAS_SC_bind: FS_RESULT(sys_unix_bind(args[1],args[2],args[3]));
    case NAS_SC_listen: FS_RESULT(sys_unix_listen(args[1],args[2]));
    case NAS_SC_connect: FS_RESULT(sys_unix_connect(args[1],args[2],args[3]));
    case NAS_SC_accept: FS_RESULT(sys_unix_accept(args[1],args[2],args[3],0));
    case NAS_SC_accept4: FS_RESULT(sys_unix_accept(args[1],args[2],args[3],args[4]));
    case NAS_SC_getsockname: FS_RESULT(sys_unix_name(args[1],args[2],args[3],false));
    case NAS_SC_getpeername: FS_RESULT(sys_unix_name(args[1],args[2],args[3],true));
    case NAS_SC_getsockopt:
        FS_RESULT(sys_unix_getsockopt(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_setsockopt: FS_RESULT((uint64_t)-ENOPROTOOPT);
    case NAS_SC_sendmsg: FS_RESULT(sys_unix_sendmsg(args[1],args[2],args[3]));
    case NAS_SC_recvmsg: FS_RESULT(sys_unix_recvmsg(args[1],args[2],args[3]));
    case NAS_SC_fcntl: FS_RESULT(sys_fcntl(args[1], args[2], args[3]));
    case NAS_SC_flock:
        if(args[1]>=NAS_FD_LIMIT || descriptors[args[1]]!=FD_TMP)
            FS_RESULT((uint64_t)-EBADF);
        FS_RESULT((uint64_t)(int64_t)tmp_flock(file_slot[args[1]],
                                              (unsigned)args[2]));
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
    case NAS_SC_chdir: FS_RESULT(sys_chdir(args[1]));
    case NAS_SC_fchdir: FS_RESULT(sys_fchdir(args[1]));
    case NAS_SC_close:
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
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
