/* Linux AF_UNIX and loopback-only AF_INET SOCK_STREAM userspace ABI adapter.
 * Owns socket arguments and user copies; unix_socket.c and ipv4_loopback.c
 * own endpoint state, pipe.c owns bytes, fd.c owns descriptions. No external
 * NIC/routing or ISA-specific syscall numbers enter this layer. */
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../task.h"
#include "../fd.h"
#include "../pipe.h"
#include "lookup.h"
#include "vfs.h"
#include "../lib/string.h"
#include "../mm/usercopy.h"
#include "../panic.h"
#include "io.h"
#include "file_ops.h"
#include "unix_socket.h"
#include "../net/ipv4_loopback.h"
#include "syscall.h"
#include "socket_syscall.h"
#include "path_request.h"
#define TASK (*nas_current_task)

/* AF_UNIX SOCK_STREAM: same global refcounted open descriptions as pipe2
 * and socketpair. A listener owns pending accepted slots, never a task-local
 * FD until accept transfers ownership. All ISA-specific numbers are decoded
 * above this shared implementation. */
static bool unix_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind==FD_SOCKET &&
           TASK.fds->entries[fd].slot<NAS_OPEN_FILE_LIMIT &&
           !nas_ipv4_socket_active(TASK.fds->entries[fd].slot);
}
static bool ipv4_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind==FD_SOCKET &&
           nas_ipv4_socket_active(TASK.fds->entries[fd].slot);
}
static uint64_t sys_socket(uint64_t domain,uint64_t type,uint64_t protocol) {
    if(domain!=1 && domain!=2)return (uint64_t)-EAFNOSUPPORT;
    if((type&15U)!=1U || (type&~(15ULL|0x800ULL|0x80000ULL)))
        return (uint64_t)-ESOCKTNOSUPPORT;
    if(protocol && !(domain==2 && protocol==6))
        return (uint64_t)-EPROTONOSUPPORT;
    unsigned fds[2],slots[2];
    int capacity=nas_fd_reserve(nas_current_task,0,1,fds,slots,"socket");
    if(capacity)return (uint64_t)(int64_t)capacity;
    unsigned fd=fds[0],slot=slots[0];
    if(domain==1)nas_unix_socket_init(slot);
    else nas_ipv4_socket_init(slot);
    if(!nas_fd_install_new(nas_current_task,fd,slot,FD_SOCKET,
                           2ULL|(type&0x800ULL),(type&0x80000ULL)!=0))
        nas_fail("reserved socket descriptor became occupied");
    return fd;
}
/* sockaddr_in: all supported Linux LP64 architectures use native 16-bit
 * AF_INET followed by network-order port/IPv4 bytes. Do not cast a user
 * pointer to a kernel sockaddr or trust its optional trailing padding. */
static int ipv4_endpoint(uint64_t address,uint64_t size,
                         struct nas_ipv4_endpoint *out) {
    uint8_t bytes[16];
    if(!out || size<sizeof(bytes))return -EINVAL;
    if(!nas_user_read_bytes(address,bytes,sizeof(bytes)))return -EFAULT;
    if(bytes[0]!=2 || bytes[1])return -EAFNOSUPPORT;
    out->port=(uint16_t)((unsigned)bytes[2]<<8|bytes[3]);
    out->address=(uint32_t)bytes[4]<<24|(uint32_t)bytes[5]<<16|
                 (uint32_t)bytes[6]<<8|bytes[7];
    return 0;
}
static void ipv4_sockaddr(struct nas_ipv4_endpoint ep,uint8_t bytes[16]) {
    memset(bytes,0,16);
    bytes[0]=2;
    bytes[2]=(uint8_t)(ep.port>>8);bytes[3]=(uint8_t)ep.port;
    bytes[4]=(uint8_t)(ep.address>>24);bytes[5]=(uint8_t)(ep.address>>16);
    bytes[6]=(uint8_t)(ep.address>>8);bytes[7]=(uint8_t)ep.address;
}
static uint64_t sys_ipv4_bind(uint64_t fd,uint64_t addr,uint64_t len) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    struct nas_ipv4_endpoint ep;
    int rc=ipv4_endpoint(addr,len,&ep);
    if(!rc)rc=nas_ipv4_socket_bind(TASK.fds->entries[fd].slot,ep);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_ipv4_connect(uint64_t fd,uint64_t addr,uint64_t len) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    struct nas_ipv4_endpoint ep;
    int rc=ipv4_endpoint(addr,len,&ep);
    if(!rc)rc=nas_ipv4_socket_connect(TASK.fds->entries[fd].slot,ep);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_ipv4_listen(uint64_t fd,uint64_t backlog) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    return (uint64_t)(int64_t)nas_ipv4_socket_listen(
        TASK.fds->entries[fd].slot,backlog>16?16U:(unsigned)backlog);
}
/* Linux sockaddr_un has a native two-byte family followed by the pathname.
 * This first backend supports Linux pathname sockets, not abstract names. */
static int unix_pathname(uint64_t addr,uint64_t length,char out[109]) {
    uint8_t buffer[110];
    if(length<3 || length>sizeof(buffer))return -EINVAL;
    if(!nas_user_read_bytes(addr,buffer,length))return -EFAULT;
    if(buffer[0]!=1 || buffer[1])return -EAFNOSUPPORT;
    if(!buffer[2])return -EOPNOTSUPP;
    unsigned n=0;
    for(;n<length-2 && buffer[n+2] && n<108;n++)
        out[n]=(char)buffer[n+2];
    out[n]=0;
    return 0;
}
static uint64_t sys_unix_bind(uint64_t fd,uint64_t address,uint64_t size) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    char path[109];
    int rc=unix_pathname(address,size,path);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_fs_path_request request;
    rc=nas_fs_kernel_path_begin((uint64_t)-100,path,&request);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_vfs_path parent={0};
    char leaf[256];
    rc=nas_fs_lookup_parent_at(request.start.dentry?&request.start:NULL,
                               request.root.dentry?&request.root:NULL,
                               request.pathname,&parent,leaf);
    if(!rc)rc=nas_unix_socket_bind(TASK.fds->entries[fd].slot,&parent,leaf,
                                   0777U & ~(uint32_t)TASK.fs->umask,path);
    if(parent.dentry && nas_vfs_path_put(&parent))
        nas_fail("UNIX bind parent Path ownership corrupted");
    nas_fs_path_end(&request);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_unix_listen(uint64_t fd,uint64_t backlog) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    return (uint64_t)(int64_t)nas_unix_socket_listen(TASK.fds->entries[fd].slot,
                                                 backlog>16?16U:(unsigned)backlog);
}
static uint64_t sys_unix_connect(uint64_t fd,uint64_t address,uint64_t size) {
    if(!unix_fd(fd))return (uint64_t)-EBADF;
    char path[109];
    int rc=unix_pathname(address,size,path);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_fs_path_request request;
    rc=nas_fs_kernel_path_begin((uint64_t)-100,path,&request);
    if(rc)return (uint64_t)(int64_t)rc;
    struct nas_vfs_path found={0};
    rc=nas_fs_lookup_path_at(request.start.dentry?&request.start:NULL,
                             request.root.dentry?&request.root:NULL,
                             request.pathname,true,&found);
    if(!rc)rc=nas_unix_socket_connect(TASK.fds->entries[fd].slot,&found);
    if(found.dentry && nas_vfs_path_put(&found))
        nas_fail("UNIX connect result Path ownership corrupted");
    nas_fs_path_end(&request);
    return (uint64_t)(int64_t)rc;
}
static uint64_t sys_unix_accept_slot(unsigned listener_slot,uint64_t address,
                                     uint64_t length_ptr,uint64_t flags) {
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
    int rc=nas_unix_socket_accept(listener_slot,&slot);
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
static uint64_t sys_ipv4_accept_slot(unsigned listener_slot,uint64_t address,
                                     uint64_t length_ptr,uint64_t flags) {
    if(flags & ~(0x800ULL|0x80000ULL))return (uint64_t)-EINVAL;
    if((address&&!length_ptr)||(!address&&length_ptr))return (uint64_t)-EFAULT;
    uint32_t length=0;
    if(length_ptr && !nas_user_read_bytes(length_ptr,(uint8_t *)&length,4))
        return (uint64_t)-EFAULT;
    if(length_ptr && (!nas_user_prepare_write(length_ptr,4) ||
                      (length && !nas_user_prepare_write(
                          address,length>16?16:length))))
        return (uint64_t)-EFAULT;
    unsigned nextfd=nas_fd_first_unused(nas_current_task,0);
    if(nextfd==NAS_FD_LIMIT)return (uint64_t)-EMFILE;
    unsigned slot;
    int rc=nas_ipv4_socket_accept(listener_slot,&slot);
    if(rc)return (uint64_t)(int64_t)rc;
    if(!nas_fd_attach_owned(nas_current_task,nextfd,slot,FD_SOCKET,
                            (flags&0x80000ULL)!=0))
        nas_fail("accepted IPv4 socket violated FD ownership");
    nas_open_files[slot].status_flags|=flags&0x800ULL;
    if(length_ptr) {
        struct nas_ipv4_endpoint peer;
        if(nas_ipv4_socket_name(slot,true,&peer))
            nas_fail("accepted IPv4 peer has no address");
        uint8_t bytes[16];
        ipv4_sockaddr(peer,bytes);
        uint32_t actual=16;
        if((length && !nas_user_write_bytes(address,bytes,
                                            length>16?16:length)) ||
           !nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4)) {
            nas_fs_close_fd(nextfd);
            return (uint64_t)-EFAULT;
        }
    }
    return nextfd;
}
/* A blocking accept pins this exact open description across scheduler
 * sleeps. A sibling may close/dup2 the original integer FD while we wait;
 * completion must still dequeue from the original listener, not the new FD. */
uint64_t nas_fs_socket_accept_description(unsigned listener_slot,
                                           uint64_t address,
                                           uint64_t length_ptr,uint64_t flags) {
    if(listener_slot>=NAS_OPEN_FILE_LIMIT ||
       !nas_open_files[listener_slot].refcount)
        return (uint64_t)-EBADF;
    if(nas_ipv4_socket_active(listener_slot))
        return sys_ipv4_accept_slot(listener_slot,address,length_ptr,flags);
    if(nas_unix_socket_listening(listener_slot))
        return sys_unix_accept_slot(listener_slot,address,length_ptr,flags);
    return (uint64_t)-EINVAL;
}
static uint64_t sys_socket_accept_fd(uint64_t fd,uint64_t address,
                                     uint64_t length_ptr,uint64_t flags) {
    if(!unix_fd(fd) && !ipv4_fd(fd))return (uint64_t)-EBADF;
    return nas_fs_socket_accept_description(TASK.fds->entries[fd].slot,
                                             address,length_ptr,flags);
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
static uint64_t sys_ipv4_name(uint64_t fd,uint64_t address,
                              uint64_t length_ptr,bool peer) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    if(!address || !length_ptr)return (uint64_t)-EFAULT;
    uint32_t length;
    if(!nas_user_read_bytes(length_ptr,(uint8_t *)&length,4))
        return (uint64_t)-EFAULT;
    struct nas_ipv4_endpoint ep={0};
    int rc=nas_ipv4_socket_name(TASK.fds->entries[fd].slot,peer,&ep);
    if(rc)return (uint64_t)(int64_t)rc;
    uint8_t output[16];
    ipv4_sockaddr(ep,output);
    uint32_t actual=16;
    uint32_t n=length>actual?actual:length;
    if(!nas_user_prepare_write(length_ptr,4) ||
       (n && !nas_user_prepare_write(address,n)))return (uint64_t)-EFAULT;
    return nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4) &&
           (!n || nas_user_write_bytes(address,output,n))
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
static uint64_t sys_ipv4_getsockopt(uint64_t fd,uint64_t level,
                                    uint64_t opt,uint64_t address,
                                    uint64_t size_ptr) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    uint32_t length;
    if(!size_ptr || !nas_user_read_bytes(size_ptr,(uint8_t *)&length,4))
        return (uint64_t)-EFAULT;
    if(length<4)return (uint64_t)-EINVAL;
    uint32_t value=0;
    if(level==1) {
        switch(opt) {
        case 2: {
            bool reuse=false;
            int rc=nas_ipv4_socket_reuseaddr(TASK.fds->entries[fd].slot,
                                             false,&reuse);
            if(rc)return (uint64_t)(int64_t)rc;
            value=reuse?1:0;break;
        }
        case 3:value=1;break; /* SO_TYPE: SOCK_STREAM */
        case 4:value=0;break; /* SO_ERROR: synchronous local connects */
        case 7:case 8:value=NAS_PIPE_CAP;break; /* actual bounded byte rings */
        case 30:value=nas_ipv4_socket_listening(
            TASK.fds->entries[fd].slot)?1:0;break; /* SO_ACCEPTCONN */
        case 38:value=6;break; /* SO_PROTOCOL: TCP */
        case 39:value=2;break; /* SO_DOMAIN: AF_INET */
        default:return (uint64_t)-ENOPROTOOPT;
        }
    } else if(level==6 && opt==1) {
        /* No packetization or Nagle queue exists inside this local stream:
         * every admitted write is forwarded immediately. */
        value=1; /* TCP_NODELAY is inherently active */
    } else return (uint64_t)-ENOPROTOOPT;
    uint32_t actual=4;
    if(!nas_user_prepare_write(address,4) ||
       !nas_user_prepare_write(size_ptr,4))return (uint64_t)-EFAULT;
    return nas_user_write_bytes(address,(const uint8_t *)&value,4) &&
           nas_user_write_bytes(size_ptr,(const uint8_t *)&actual,4)
         ? 0:(uint64_t)-EFAULT;
}
static uint64_t sys_ipv4_setsockopt(uint64_t fd,uint64_t level,
                                    uint64_t opt,uint64_t address,
                                    uint64_t length) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    if(length<4 || !address)return (uint64_t)-EINVAL;
    uint32_t value;
    if(!nas_user_read_bytes(address,(uint8_t *)&value,4))
        return (uint64_t)-EFAULT;
    if(level==1 && opt==2) { /* SO_REUSEADDR */
        bool enable=value!=0;
        return (uint64_t)(int64_t)nas_ipv4_socket_reuseaddr(
            TASK.fds->entries[fd].slot,true,&enable);
    }
    if(level==6 && opt==1) /* TCP_NODELAY: immediate stream forwarding */
        return value ? 0:(uint64_t)-EOPNOTSUPP;
    return (uint64_t)-ENOPROTOOPT;
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
static uint64_t sys_ipv4_sendmsg(uint64_t fd,uint64_t address,uint64_t flags) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    /* Per-call MSG_DONTWAIT needs a dedicated File transfer flag, not a
     * temporary mutation of the shared open-description O_NONBLOCK bit. */
    if(flags & ~0x4000ULL)return (uint64_t)-EOPNOTSUPP;
    struct nas_msghdr msg;
    if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)))
        return (uint64_t)-EFAULT;
    if(msg.name)return (uint64_t)-EISCONN;
    if(msg.controllen)return (uint64_t)-EOPNOTSUPP;
    return unix_msgiov(fd,&msg,true); /* same owned stream File data plane */
}
static uint64_t sys_ipv4_recvmsg(uint64_t fd,uint64_t address,uint64_t flags) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    if(flags)return (uint64_t)-EOPNOTSUPP;
    struct nas_msghdr msg;
    if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)) ||
       !nas_user_prepare_write(address,sizeof(msg)))
        return (uint64_t)-EFAULT;
    uint8_t peer_addr[16];
    if(msg.name) {
        struct nas_ipv4_endpoint peer;
        int rc=nas_ipv4_socket_name(TASK.fds->entries[fd].slot,true,&peer);
        if(rc)return (uint64_t)(int64_t)rc;
        ipv4_sockaddr(peer,peer_addr);
        uint32_t n=msg.namelen>16?16:msg.namelen;
        if(n && !nas_user_prepare_write(msg.name,n))
            return (uint64_t)-EFAULT;
    }
    uint64_t received=unix_msgiov(fd,&msg,false);
    if((int64_t)received<0)return received;
    if(msg.name) {
        uint32_t n=msg.namelen>16?16:msg.namelen;
        if(n && !nas_user_write_bytes(msg.name,peer_addr,n))
            return (uint64_t)-EFAULT;
        msg.namelen=16;
    } else msg.namelen=0;
    msg.controllen=0;
    msg.flags=0;
    return nas_user_write_bytes(address,(const uint8_t *)&msg,sizeof(msg))
         ? received:(uint64_t)-EFAULT;
}
static uint64_t sys_ipv4_sendto(uint64_t fd,uint64_t buffer,
                                uint64_t length,uint64_t flags,
                                uint64_t destination,uint64_t address_size) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    if(flags & ~0x4000ULL)return (uint64_t)-EOPNOTSUPP;
    if(destination || address_size)return (uint64_t)-EISCONN;
    return nas_fs_fd_write(fd,buffer,length);
}
static uint64_t sys_ipv4_recvfrom(uint64_t fd,uint64_t buffer,
                                  uint64_t length,uint64_t flags,
                                  uint64_t address,uint64_t length_ptr) {
    if(!ipv4_fd(fd))return (uint64_t)-EBADF;
    if(flags)return (uint64_t)-EOPNOTSUPP;
    if(address&&!length_ptr)return (uint64_t)-EFAULT;
    uint32_t actual=16,capacity=0;
    uint8_t bytes[16];
    if(address) {
        if(!nas_user_read_bytes(length_ptr,(uint8_t *)&capacity,4))
            return (uint64_t)-EFAULT;
        struct nas_ipv4_endpoint peer;
        int rc=nas_ipv4_socket_name(TASK.fds->entries[fd].slot,true,&peer);
        if(rc)return (uint64_t)(int64_t)rc;
        ipv4_sockaddr(peer,bytes);
        if(!nas_user_prepare_write(length_ptr,4) ||
           (capacity && !nas_user_prepare_write(address,
                                                capacity>16?16:capacity)))
            return (uint64_t)-EFAULT;
    }
    uint64_t received=nas_fs_fd_read(fd,buffer,length);
    if((int64_t)received<0)return received;
    if(address && (!nas_user_write_bytes(length_ptr,
                                            (const uint8_t *)&actual,4) ||
                      (capacity && !nas_user_write_bytes(address,bytes,
                                              capacity>16?16:capacity))))
        return (uint64_t)-EFAULT;
    return received;
}
static uint64_t sys_socket_shutdown(uint64_t fd,uint64_t how) {
    if(!unix_fd(fd) && !ipv4_fd(fd))return (uint64_t)-EBADF;
    if(how>2ULL)return (uint64_t)-EINVAL;
    unsigned slot=TASK.fds->entries[fd].slot;
    if(ipv4_fd(fd)) {
        struct nas_ipv4_endpoint peer;
        int rc=nas_ipv4_socket_name(slot,true,&peer);
        if(rc)return (uint64_t)(int64_t)rc;
    }
    return (uint64_t)(int64_t)nas_pipe_socket_shutdown(slot,(unsigned)how);
}

#define SOCKET_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_socket_syscall(enum nas_syscall_op op,
                                         const uint64_t *args) {
    switch (op) {
    case NAS_SC_socket: SOCKET_RESULT(sys_socket(args[1],args[2],args[3]));
    case NAS_SC_bind: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_bind(args[1],args[2],args[3]) :
        sys_unix_bind(args[1],args[2],args[3]));
    case NAS_SC_listen: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_listen(args[1],args[2]) : sys_unix_listen(args[1],args[2]));
    case NAS_SC_connect: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_connect(args[1],args[2],args[3]) :
        sys_unix_connect(args[1],args[2],args[3]));
    case NAS_SC_accept: SOCKET_RESULT(sys_socket_accept_fd(
        args[1],args[2],args[3],0));
    case NAS_SC_accept4: SOCKET_RESULT(sys_socket_accept_fd(
        args[1],args[2],args[3],args[4]));
    case NAS_SC_getsockname: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_name(args[1],args[2],args[3],false) :
        sys_unix_name(args[1],args[2],args[3],false));
    case NAS_SC_getpeername: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_name(args[1],args[2],args[3],true) :
        sys_unix_name(args[1],args[2],args[3],true));
    case NAS_SC_getsockopt:
        SOCKET_RESULT(ipv4_fd(args[1]) ?
            sys_ipv4_getsockopt(args[1],args[2],args[3],args[4],args[5]) :
            sys_unix_getsockopt(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_setsockopt: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_setsockopt(args[1],args[2],args[3],args[4],args[5]) :
        (uint64_t)-ENOPROTOOPT);
    case NAS_SC_sendmsg: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_sendmsg(args[1],args[2],args[3]) :
        sys_unix_sendmsg(args[1],args[2],args[3]));
    case NAS_SC_recvmsg: SOCKET_RESULT(ipv4_fd(args[1]) ?
        sys_ipv4_recvmsg(args[1],args[2],args[3]) :
        sys_unix_recvmsg(args[1],args[2],args[3]));
    case NAS_SC_sendto: SOCKET_RESULT(sys_ipv4_sendto(args[1],args[2],args[3],
                                                       args[4],args[5],args[6]));
    case NAS_SC_recvfrom: SOCKET_RESULT(sys_ipv4_recvfrom(args[1],args[2],args[3],
                                                           args[4],args[5],args[6]));
    case NAS_SC_shutdown: SOCKET_RESULT(sys_socket_shutdown(args[1],args[2]));
    default: return (struct nas_fs_result){ .handled=false, .value=0 };
    }
}
#undef SOCKET_RESULT
