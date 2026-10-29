/* Linux AF_UNIX pathname/stream userspace ABI adapter.
 * Owns socket syscall decoding and user copies; unix_socket.c owns
 * listener/endpoint state, pipe.c owns bytes, fd.c owns descriptions.
 * Hardware and ISA-specific syscall numbers never enter this layer. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../path.h"
#include "../bootfs.h"
#include "lookup.h"
#include "vfs.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../arch/mmu.h"
#include "../panic.h"
#include "io.h"
#include "file_ops.h"
#include "unix_socket.h"
#include "syscall.h"
#include "socket_syscall.h"
#include "path_request.h"
#define TASK (*nas_current_task)
#define current_workdir TASK.fs->cwd

/* AF_UNIX SOCK_STREAM: same global refcounted open descriptions as pipe2
 * and socketpair. A listener owns pending accepted slots, never a task-local
 * FD until accept transfers ownership. All ISA-specific numbers are decoded
 * above this shared implementation. */
static bool unix_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind==FD_SOCKET &&
           TASK.fds->entries[fd].slot<NAS_OPEN_FILE_LIMIT;
}
static uint64_t sys_socket(uint64_t domain,uint64_t type,uint64_t protocol) {
    if(domain!=1)return (uint64_t)-EAFNOSUPPORT;
    if((type&15U)!=1U || (type&~(15ULL|0x800ULL|0x80000ULL)))
        return (uint64_t)-ESOCKTNOSUPPORT;
    if(protocol)return (uint64_t)-EPROTONOSUPPORT;
    unsigned fds[2],slots[2];
    int capacity=nas_fd_reserve(nas_current_task,0,1,fds,slots,"socket");
    if(capacity)return (uint64_t)(int64_t)capacity;
    unsigned fd=fds[0],slot=slots[0];
    nas_unix_socket_init(slot);
    if(!nas_fd_install_new(nas_current_task,fd,slot,FD_SOCKET,
                           2ULL|(type&0x800ULL),(type&0x80000ULL)!=0))
        nas_fail("reserved socket descriptor became occupied");
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
    struct nas_fs_path_request request;
    int rc=nas_fs_kernel_path_begin((uint64_t)-100,input,&request);
    if(rc)return rc;
    struct nas_vfs_path parent={0};char leaf[256],base[256];
    rc=nas_fs_lookup_parent_at(request.start.dentry?&request.start:NULL,
                               request.root.dentry?&request.root:NULL,
                               request.pathname,&parent,leaf);
    if(!rc)rc=nas_vfs_path_string(&parent,base);
    if(!rc)rc=nas_vfs_join_child(base,leaf,out);
    if(parent.dentry && nas_vfs_path_put(&parent))
        nas_fail("unix address parent Path corrupted");
    nas_fs_path_end(&request);
    if(rc==-ENOENT && !bootfs_mounted()) {
        /* Rootless boot compatibility. The current UNIX socket backend
         * still owns tmpfs pathname strings, not generic VFS socket inodes;
         * this does not claim AF_UNIX has become an inode-owned backend. */
        const char *origin=NULL;
        if(input[0]!='/')origin=current_workdir;
        char raw[256];
        int legacy=nas_path_join_raw(origin,input,raw);
        if(legacy)return legacy;
        memcpy(out,raw,strlen(raw)+1);
        return 0;
    }
    return rc;
}
static uint64_t sys_unix_bind(uint64_t fd,uint64_t address,uint64_t size) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    char path[256];int err=unix_address(address,size,path);
    if(err)return (uint64_t)(int64_t)err;
    return (uint64_t)(int64_t)nas_unix_socket_bind(
        TASK.fds->entries[fd].slot,path,0777U & ~(uint32_t)TASK.fs->umask);
}
static uint64_t sys_unix_listen(uint64_t fd,uint64_t backlog) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    return (uint64_t)(int64_t)nas_unix_socket_listen(TASK.fds->entries[fd].slot,
                                                 backlog>16?16U:(unsigned)backlog);
}
static uint64_t sys_unix_connect(uint64_t fd,uint64_t address,uint64_t size) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    char path[256];int err=unix_address(address,size,path);
    if(err)return (uint64_t)(int64_t)err;
    return (uint64_t)(int64_t)nas_unix_socket_connect(TASK.fds->entries[fd].slot,path);
}
static uint64_t sys_unix_accept(uint64_t fd,uint64_t address,
                                 uint64_t length_ptr,uint64_t flags) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    if(flags & ~(0x800ULL|0x80000ULL))return (uint64_t)-EINVAL;
    if((address&&!length_ptr)||(!address&&length_ptr))return (uint64_t)-EFAULT;
    uint32_t length=0;
    if(length_ptr && !nas_user_read_bytes(length_ptr,(uint8_t *)&length,4))
        return (uint64_t)-EFAULT;
    /* Validate both output objects before the listener dequeues a pending
     * connection or the process publishes a new FD. Resident != writable:
     * read-only, COW and lazy mapped user pages need their VMA policy. */
    if(length_ptr && (!nas_user_prepare_write(length_ptr,4) ||
                      (length && !nas_user_prepare_write(
                          address,length>2?2:length))))
        return (uint64_t)-EFAULT;
    unsigned nextfd=nas_fd_first_unused(nas_current_task,0);
    if(nextfd==NAS_FD_LIMIT)return (uint64_t)-EMFILE;
    unsigned slot;
    int rc=nas_unix_socket_accept(TASK.fds->entries[fd].slot,&slot);
    if(rc)return (uint64_t)(int64_t)rc;
    if(!nas_fd_attach_owned(nas_current_task,nextfd,slot,FD_SOCKET,
                            (flags&0x80000ULL)!=0))
        nas_fail("accepted socket ownership transfer violated FD invariant");
    nas_open_files[slot].status_flags|=flags&0x800ULL;
    if(length_ptr) {
        uint32_t actual=2;
        uint8_t family[2]={1,0};
        if((length && !nas_user_write_bytes(address,family,length>2?2:length)) ||
           !nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4)) {
            /* No ordinary user pointer can halt the kernel. Should the
             * address space change after preflight, unwind the FD slot. */
            nas_fs_close_fd(nextfd);
            return (uint64_t)-EFAULT;
        }
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
    const char *path=peer ? 0 : nas_unix_socket_name(TASK.fds->entries[fd].slot);
    uint32_t actual=2;
    if(path) {
        unsigned n=(unsigned)strnlen(path,107);
        memcpy(output+2,path,n);
        output[n+2]=0;
        actual=n+3;
    }
    if(size>actual)size=actual;
    if(!nas_user_prepare_write(length_ptr,4) ||
       (size && !nas_user_prepare_write(address,size)))
        return (uint64_t)-EFAULT;
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
    if(opt==17) { /* SO_PEERCRED: Linux struct ucred {pid,uid,gid}. */
        uint32_t credentials[3];
        int rc=nas_unix_socket_peercred(TASK.fds->entries[fd].slot,credentials);
        if(rc)return (uint64_t)(int64_t)rc;
        if(size<sizeof(credentials))return (uint64_t)-EINVAL;
        size=sizeof(credentials);
        if(!nas_user_prepare_write(address,size) ||
           !nas_user_prepare_write(size_ptr,4))return (uint64_t)-EFAULT;
        return nas_user_write_bytes(address,(const uint8_t *)credentials,size) &&
               nas_user_write_bytes(size_ptr,(const uint8_t *)&size,4)
             ? 0:(uint64_t)-EFAULT;
    }
    if(size<4)return (uint64_t)-EINVAL;
    uint32_t value;
    if(opt==3)value=1; /* SO_TYPE: stream */
    else if(opt==4)value=0; /* SO_ERROR: no pending asynchronous error */
    else return (uint64_t)-ENOPROTOOPT;
    size=4;
    if(!nas_user_prepare_write(address,4) ||
       !nas_user_prepare_write(size_ptr,4))return (uint64_t)-EFAULT;
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
               TASK.fds->entries[transferred].kind==FD_CLOSED)return (uint64_t)-EBADF;
            kinds[i]=TASK.fds->entries[transferred].kind;
            slots[i]=TASK.fds->entries[transferred].slot;
        }
        if(!nas_unix_rights_available(TASK.fds->entries[fd].slot,rights_count))
            return (uint64_t)-EAGAIN;
    }
    uint64_t wrote=unix_msgiov(fd,&msg,true);
    if((int64_t)wrote<0)return wrote;
    if(rights_count && wrote) {
        int rc=nas_unix_rights_send(TASK.fds->entries[fd].slot,kinds,slots,rights_count);
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
    if(!nas_user_prepare_write(address,sizeof(msg)))
        return (uint64_t)-EFAULT;
    if(msg.iovlen>32)return (uint64_t)-EMSGSIZE;
    unsigned pending=nas_unix_rights_count(TASK.fds->entries[fd].slot);
    unsigned deliver=0;
    if(msg.control && msg.controllen>=20) {
        /* Clamp in uint64_t before narrowing an untrusted controllen.
         * UINT64_MAX must not wrap the deliverable rights count to zero. */
        uint64_t capacity=(msg.controllen-16)/4;
        if(capacity>NAS_UNIX_MAX_RIGHTS)capacity=NAS_UNIX_MAX_RIGHTS;
        deliver=(unsigned)capacity;
    }
    if(deliver>pending)deliver=pending;
    if(deliver && !nas_user_prepare_write(
        msg.control,16ULL+(uint64_t)deliver*4ULL))
        return (uint64_t)-EFAULT;
    unsigned free_fd=0;
    for(unsigned i=3;i<NAS_FD_LIMIT;i++)
        if(TASK.fds->entries[i].kind==FD_CLOSED)free_fd++;
    if(deliver>free_fd)return (uint64_t)-EMFILE;
    uint64_t received=unix_msgiov(fd,&msg,false);
    if((int64_t)received<0)return received;
    msg.namelen=0;
    msg.flags=pending>deliver ? 0x8U : 0U; /* MSG_CTRUNC */
    msg.controllen=0;
    unsigned installed[NAS_UNIX_MAX_RIGHTS];
    unsigned installed_count=0;
    if(deliver && received) {
        uint8_t kinds[NAS_UNIX_MAX_RIGHTS],slots[NAS_UNIX_MAX_RIGHTS];
        unsigned count=nas_unix_rights_receive(TASK.fds->entries[fd].slot,kinds,slots,deliver);
        uint8_t control[16+NAS_UNIX_MAX_RIGHTS*4]={0};
        uint64_t clen=16+(uint64_t)count*4;
        uint32_t level=1,type=1;
        memcpy(control,&clen,8);
        memcpy(control+8,&level,4);memcpy(control+12,&type,4);
        unsigned done=0;
        for(unsigned i=3;i<NAS_FD_LIMIT && done<count;i++) {
            if(TASK.fds->entries[i].kind!=FD_CLOSED)continue;
            if(!nas_fd_attach_owned(nas_current_task,i,slots[done],
                                    kinds[done],(flags&0x40000000ULL)!=0))
                nas_fail("received SCM_RIGHTS slot violated FD invariant");
            installed[installed_count++]=i;
            int32_t number=(int32_t)i;
            memcpy(control+16+done*4,&number,4);
            done++;
        }
        msg.controllen=(clen+7ULL)&~7ULL;
        if(!nas_user_write_bytes(msg.control,control,(size_t)clen))
            goto bad_user_output;
    }
    if(!nas_user_write_bytes(address,(const uint8_t *)&msg,sizeof(msg)))
        goto bad_user_output;
    return received;
bad_user_output:
    /* Invalidation after preflight must not strand rights in an FD table
     * that userspace never learned about. The consumed message cannot be
     * requeued, but its transferred references can always be released. */
    for(unsigned i=0;i<installed_count;i++)nas_fs_close_fd(installed[i]);
    return (uint64_t)-EFAULT;
}

#define SOCKET_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_socket_syscall(enum nas_syscall_op op,
                                         const uint64_t *args) {
    switch (op) {
    case NAS_SC_socket: SOCKET_RESULT(sys_socket(args[1],args[2],args[3]));
    case NAS_SC_bind: SOCKET_RESULT(sys_unix_bind(args[1],args[2],args[3]));
    case NAS_SC_listen: SOCKET_RESULT(sys_unix_listen(args[1],args[2]));
    case NAS_SC_connect: SOCKET_RESULT(sys_unix_connect(args[1],args[2],args[3]));
    case NAS_SC_accept: SOCKET_RESULT(sys_unix_accept(args[1],args[2],args[3],0));
    case NAS_SC_accept4: SOCKET_RESULT(sys_unix_accept(args[1],args[2],args[3],args[4]));
    case NAS_SC_getsockname: SOCKET_RESULT(sys_unix_name(args[1],args[2],args[3],false));
    case NAS_SC_getpeername: SOCKET_RESULT(sys_unix_name(args[1],args[2],args[3],true));
    case NAS_SC_getsockopt:
        SOCKET_RESULT(sys_unix_getsockopt(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_setsockopt: SOCKET_RESULT((uint64_t)-ENOPROTOOPT);
    case NAS_SC_sendmsg: SOCKET_RESULT(sys_unix_sendmsg(args[1],args[2],args[3]));
    case NAS_SC_recvmsg: SOCKET_RESULT(sys_unix_recvmsg(args[1],args[2],args[3]));
    default: return (struct nas_fs_result){ .handled=false, .value=0 };
    }
}
#undef SOCKET_RESULT
