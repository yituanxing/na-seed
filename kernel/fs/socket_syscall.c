/* Shared Linux AF_UNIX / AF_INET userspace socket ABI adapter. AF_UNIX
 * remains an owned UNIX File; AF_INET uses the upstream lwIP OpenFile
 * provider on every ISA. This layer parses Linux syscall arguments and sockaddr.
 * No NIC policy, Netconn waits or ISA syscall numbers enter here. */
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
#include "syscall.h"
#include "socket_syscall.h"
#include "path_request.h"
#include "../../ports/lwip/socket_port.h"
#include "../../ports/lwip/network_boot.h"
#include "netlink_route.h"
#define TASK (*nas_current_task)

/* Only this Linux ABI adapter decodes the network-order sockaddr_in wire
 * format; Netconn and the driver never own this host-order stack value. */
struct nas_ipv4_endpoint { uint32_t address; uint16_t port; };

static bool lwip_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind==FD_SOCKET &&
           nas_lwip_socket_is_slot(TASK.fds->entries[fd].slot);
}
static bool netlink_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind==FD_SOCKET &&
           nas_netlink_route_is_slot(TASK.fds->entries[fd].slot);
}
static struct nas_lwip_bridge_address lwip_address(
                                     struct nas_ipv4_endpoint endpoint) {
    return (struct nas_lwip_bridge_address){
        .ipv4=endpoint.address,.port=endpoint.port
    };
}
/* OPEN is asynchronous. Subsequent local control submissions can queue
 * behind it, but a full worker queue is not a successful parked command.
 * Only a truly queued command reports the Task-block sentinel -EAGAIN. */
static uint64_t lwip_control_result(int rc) {
    return (uint64_t)(int64_t)(rc==-EAGAIN ? -ENOBUFS : rc ? rc : -EAGAIN);
}

/* AF_UNIX SOCK_STREAM: same global refcounted open descriptions as pipe2
 * and socketpair. A listener owns pending accepted slots, never a task-local
 * FD until accept transfers ownership. All ISA-specific numbers are decoded
 * above this shared implementation. */
static bool unix_fd(uint64_t fd) {
    return fd<NAS_FD_LIMIT && TASK.fds->entries[fd].kind==FD_SOCKET &&
           TASK.fds->entries[fd].slot<NAS_OPEN_FILE_LIMIT
           && !nas_lwip_socket_is_slot(TASK.fds->entries[fd].slot)
           && !nas_netlink_route_is_slot(TASK.fds->entries[fd].slot)
           ;
}
static bool inet_fd(uint64_t fd) {
    return lwip_fd(fd);
}
static uint64_t sys_socket(uint64_t domain,uint64_t type,uint64_t protocol) {
    if(domain!=1 && domain!=2
       && domain!=16 /* Linux AF_NETLINK */
       )return (uint64_t)-EAFNOSUPPORT;
    unsigned socket_type=(unsigned)(type&15U);
    if((socket_type!=1U
        && !(domain==2 && socket_type==2U)
        && !(domain==16 && socket_type==3U) /* SOCK_RAW NETLINK_ROUTE */
       ) || (type&~(15ULL|0x800ULL|0x80000ULL)))
        return (uint64_t)-ESOCKTNOSUPPORT;
    if(protocol && !(domain==2 && protocol==6 && socket_type==1U)
       && !(domain==2 && protocol==17 && socket_type==2U)
       )
        return (uint64_t)-EPROTONOSUPPORT;
    unsigned fds[2],slots[2];
    /* Closing Netconns retain the original indexed bridge generation until
     * native worker teardown. Generic Files may reuse zero-ref slots; a new
     * AF_INET socket must skip the still-closing native bridge slot instead
     * of reporting false ENFILE while many other slots are available. */
    int capacity=
        domain==2 ?
        nas_fd_reserve_filtered(nas_current_task,0,1,fds,slots,"socket",
                                nas_lwip_socket_slot_reusable) :
        nas_fd_reserve(nas_current_task,0,1,fds,slots,"socket");
    if(capacity)return (uint64_t)(int64_t)capacity;
    unsigned fd=fds[0],slot=slots[0];
    if(domain==1)nas_unix_socket_init(slot);
    if(!nas_fd_install_new(nas_current_task,fd,slot,FD_SOCKET,
                           2ULL|(type&0x800ULL),(type&0x80000ULL)!=0))
        nas_fail("reserved socket descriptor became occupied");
    if(domain==2) {
        nas_open_files[slot].ops=nas_lwip_socket_port_ops();
        int rc=nas_lwip_socket_open(slot,socket_type==1U ?
                                   NAS_LWIP_BRIDGE_TCP:NAS_LWIP_BRIDGE_UDP);
        if(rc) {
            nas_fs_close_fd(fd); /* same FileOps finalizer, no second table */
            return (uint64_t)(int64_t)rc;
        }
    }
    if(domain==16) {
        nas_open_files[slot].ops=nas_netlink_route_ops();
        int rc=nas_netlink_route_open(slot,
                          (uint32_t)nas_task_process_pid(nas_current_task));
        if(rc) {
            nas_fs_close_fd(fd);
            return (uint64_t)(int64_t)rc;
        }
    }
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
static int lwip_name_preflight(uint64_t address,uint64_t length_ptr,
                               uint32_t *capacity) {
    if(!address || !length_ptr || !capacity)return -EFAULT;
    if(!nas_user_read_bytes(length_ptr,(uint8_t *)capacity,4))return -EFAULT;
    unsigned n=*capacity>16?16U:*capacity;
    if(!nas_user_prepare_write(length_ptr,4) ||
       (n && !nas_user_prepare_write(address,n)))return -EFAULT;
    return 0;
}
/* Native GETNAME has its own result payload, unlike status-only control.
 * The issuing Task's virtual memory is available only in this completion,
 * never in the asynchronous Netconn worker or a foreign Task wait scan. */
uint64_t nas_fs_socket_name_description(unsigned slot,uint64_t address,
                                        uint64_t length_ptr,bool peer) {
    (void)peer; /* begin() captured the caller's local/peer selection */
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
       !nas_lwip_socket_is_slot(slot))return (uint64_t)-EBADF;
    struct nas_lwip_bridge_address name={0};
    int rc=nas_lwip_socket_getname_complete(slot,&name);
    if(rc==0)return (uint64_t)-EAGAIN;
    if(rc<0)return (uint64_t)(int64_t)rc;
    if(rc!=1)return (uint64_t)-EIO;
    /* A CLONE_VM peer can unmap the original output while this Task sleeps.
     * Consume the finished native command before revalidating user memory;
     * EFAULT must not strand PORT_CONTROL_DONE on the shared description. */
    uint32_t capacity;
    rc=lwip_name_preflight(address,length_ptr,&capacity);
    if(rc)return (uint64_t)(int64_t)rc;
    uint8_t bytes[16];
    ipv4_sockaddr((struct nas_ipv4_endpoint){name.ipv4,name.port},bytes);
    uint32_t actual=16,n=capacity>16?16:capacity;
    return nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4) &&
           (!n || nas_user_write_bytes(address,bytes,n))
         ? 0:(uint64_t)-EFAULT;
}
static uint64_t sys_ipv4_bind(uint64_t fd,uint64_t addr,uint64_t len) {
    if(lwip_fd(fd)) {
        struct nas_ipv4_endpoint ep;
        int rc=ipv4_endpoint(addr,len,&ep);
        if(rc)return (uint64_t)(int64_t)rc;
        return lwip_control_result(nas_lwip_socket_bind(
            TASK.fds->entries[fd].slot,lwip_address(ep)));
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_ipv4_connect(uint64_t fd,uint64_t addr,uint64_t len) {
    if(lwip_fd(fd)) {
        struct nas_ipv4_endpoint ep;
        int rc=ipv4_endpoint(addr,len,&ep);
        if(rc)return (uint64_t)(int64_t)rc;
        unsigned slot=TASK.fds->entries[fd].slot;
        rc=nas_lwip_socket_connect(slot,lwip_address(ep));
        if(rc)return (uint64_t)(int64_t)(rc==-EAGAIN?-ENOBUFS:rc);
        if(nas_lwip_socket_type(slot)==NAS_LWIP_BRIDGE_UDP)
            return 0; /* no UDP remote handshake; errors via SO_ERROR */
        /* SOCK_NONBLOCK observes a pending native connect. The worker,
         * not this user syscall, completes the actual TCP handshake. */
        if(nas_open_files[slot].status_flags&0x800ULL)
            return (uint64_t)-EINPROGRESS;
        return (uint64_t)-EAGAIN;
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_ipv4_listen(uint64_t fd,uint64_t backlog) {
    if(lwip_fd(fd))return lwip_control_result(nas_lwip_socket_listen(
        TASK.fds->entries[fd].slot,backlog>16U?16U:(unsigned)backlog));
    return (uint64_t)-EBADF;
}

/* A native Task parked in bind/connect/listen pins one OpenFile slot. Its
 * completion never re-decodes a possibly stale integer FD or replays a
 * duplicate network command. The FileOps owner retains the copied native
 * Netconn request and caches exactly one eventual result. */
bool nas_fs_socket_control_ready(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT && nas_open_files[slot].refcount &&
       nas_lwip_socket_is_slot(slot))
        return nas_lwip_socket_control_ready(slot)!=0;
    return false;
}
uint64_t nas_fs_socket_control_description(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
       !nas_lwip_socket_is_slot(slot))return (uint64_t)-EBADF;
    /* Completion state is separate from native errno: a completed Netconn
     * may legitimately report EINPROGRESS or EAGAIN. Only the explicit
     * 'not yet finished' state may re-arm the blocked Task continuation. */
    struct nas_lwip_bridge_result result={0};
    int rc=nas_lwip_socket_port_complete(slot,&result);
    if(rc==0)return (uint64_t)-EAGAIN;
    return (uint64_t)(int64_t)(rc==1 ? result.status:rc);
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
/* A blocked Task owns the lease, while the bridge owns the native Netconn.
 * Disposal runs for EINTR, task exit and fd unbinding; invalidate the bridge
 * request BEFORE dropping the lease so a late accept cannot resurrect it. */
static void lwip_accept_cancel_wait(unsigned listener_slot,
                                    struct nas_fd_slot_lease lease) {
    (void)nas_lwip_socket_accept_cancel(listener_slot,lease);
    (void)nas_fd_slot_lease_cancel(lease);
}
static void lwip_accept_disarm(void) {
    TASK.wait.accept_lease=(struct nas_fd_slot_lease){0};
    TASK.wait.accept_lease_cancel=NULL;
}
/* One accepted Netconn is published into the EXISTING global OpenFile pool.
 * No network stack-specific FD table, synthetic pipe, or second TCP layer. */
static uint64_t sys_lwip_accept_slot(unsigned listener_slot,uint64_t address,
                                     uint64_t length_ptr,uint64_t flags) {
    if(flags & ~(0x800ULL|0x80000ULL))return (uint64_t)-EINVAL;
    if((address&&!length_ptr)||(!address&&length_ptr))return (uint64_t)-EFAULT;
    uint32_t capacity=0;
    if(length_ptr && !nas_user_read_bytes(length_ptr,(uint8_t *)&capacity,4))
        return (uint64_t)-EFAULT;
    if(length_ptr && (!nas_user_prepare_write(length_ptr,4) ||
                      (capacity && !nas_user_prepare_write(address,
                                                  capacity>16?16:capacity))))
        return (uint64_t)-EFAULT;
    unsigned nextfd=nas_fd_first_unused(nas_current_task,0);
    if(nextfd==NAS_FD_LIMIT)return (uint64_t)-EMFILE;

    struct nas_fd_slot_lease lease=TASK.wait.accept_lease;
    struct nas_lwip_bridge_id accepted={0};
    struct nas_lwip_bridge_address peer={0};
    const bool task_owned=TASK.wait.accept_lease_cancel!=NULL;
    int rc;
    if(!task_owned) {
        /* A nonblocking listener may already own a fully accepted child:
         * poll/select initiated the native worker in the background. This
         * durable ticket belongs to the OpenFile, never to a returning Task.
         * Blocking accept also drains a ticket if F_SETFL changed meanwhile. */
        rc=nas_lwip_socket_accept_prefetched(listener_slot,&lease,
                                               &accepted,&peer);
        if(rc==1)goto publish;
        if(rc==-EAGAIN)return (uint64_t)-EAGAIN;
        if(rc<0)return (uint64_t)(int64_t)rc;
        if(nas_open_files[listener_slot].status_flags&0x800ULL) {
            rc=nas_lwip_socket_accept_prefetch_start(listener_slot);
            return (uint64_t)(int64_t)(rc==0 || rc==-EAGAIN ||
                rc==-EALREADY || rc==-EBUSY ? -EAGAIN:rc);
        }
        rc=nas_fd_slot_lease_begin(&lease,"lwip accept");
        if(rc)return (uint64_t)(int64_t)rc;
        rc=nas_lwip_socket_accept_begin(listener_slot,lease);
        if(rc) {
            (void)nas_fd_slot_lease_cancel(lease);
            return (uint64_t)(int64_t)(rc==-EALREADY || rc==-EBUSY ?
                                       -EAGAIN:rc);
        }
        TASK.wait.accept_listener_slot=listener_slot;
        TASK.wait.accept_lease=lease;
        TASK.wait.accept_lease_cancel=lwip_accept_cancel_wait;
        return (uint64_t)-EAGAIN;
    }
    if(TASK.wait.accept_listener_slot!=listener_slot ||
       !nas_fd_slot_lease_valid(lease))
        return (uint64_t)-EBADF;

    rc=nas_lwip_socket_accept_complete(listener_slot,lease,&accepted,&peer);
    if(rc==0)return (uint64_t)-EAGAIN; /* the same native command is pending */
    if(rc<0) {
        /* No accepted child was published; the native completion is already
         * consumed. The lease can now be retired without a bridge race. */
        lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)(int64_t)rc;
    }
    if(rc!=1) {
        lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)-EIO;
    }
publish:
    if(!accepted.generation || accepted.slot!=lease.slot) {
        if(accepted.generation)(void)nas_lwip_bridge_release(accepted);
        if(task_owned)lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)-EIO;
    }
    /* Upstream TCP inherits SOF_KEEPALIVE when constructing an accepted
     * child (tcp_in.c: SOF_INHERITED). Mirror that real PCB state on the
     * new OpenFile for Linux getsockopt(), including across dup/fork. */
    bool inherited_keepalive=false;
    rc=nas_lwip_socket_keepalive(listener_slot,false,&inherited_keepalive);
    if(rc) {
        (void)nas_lwip_bridge_release(accepted);
        if(task_owned)lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)(int64_t)rc;
    }
    rc=nas_lwip_socket_port_adopt(accepted,inherited_keepalive);
    if(rc) {
        (void)nas_lwip_bridge_release(accepted);
        if(task_owned)lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)(int64_t)rc;
    }
    /* The native ACCEPT bridge returned the actual peer sockaddr. Retain it
     * with the very same OpenFile being published, so subsequent TCP
     * recvfrom(..., sockaddr*) never needs to call Netconn on a Task. */
    rc=nas_lwip_socket_port_set_peer(lease.slot,peer);
    if(rc) {
        nas_lwip_socket_port_ops()->release(lease.slot);
        if(task_owned)lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)(int64_t)rc;
    }
    rc=nas_fd_slot_lease_commit(lease,nas_current_task,nextfd,FD_SOCKET,
                                2ULL|(flags&0x800ULL),
                                (flags&0x80000ULL)!=0,
                                nas_lwip_socket_port_ops());
    if(rc) {
        /* An FD number may have been claimed by a CLONE_FILES sibling
         * between preflight and publication. Never destroy its entry. */
        nas_lwip_socket_port_ops()->release(lease.slot);
        if(task_owned)lwip_accept_disarm();
        (void)nas_fd_slot_lease_cancel(lease);
        return (uint64_t)(int64_t)rc;
    }
    if(task_owned)lwip_accept_disarm(); /* accepted FD owns it now */
    if(length_ptr) {
        const struct nas_ipv4_endpoint endpoint={peer.ipv4,peer.port};
        uint8_t bytes[16];
        ipv4_sockaddr(endpoint,bytes);
        const uint32_t actual=16;
        uint32_t n=capacity>actual?actual:capacity;
        if((n && !nas_user_write_bytes(address,bytes,n)) ||
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
    if(nas_lwip_socket_is_slot(listener_slot))
        return sys_lwip_accept_slot(listener_slot,address,length_ptr,flags);
    if(nas_unix_socket_listening(listener_slot))
        return sys_unix_accept_slot(listener_slot,address,length_ptr,flags);
    return (uint64_t)-EINVAL;
}
static uint64_t sys_socket_accept_fd(uint64_t fd,uint64_t address,
                                     uint64_t length_ptr,uint64_t flags) {
    if(!unix_fd(fd) && !inet_fd(fd))return (uint64_t)-EBADF;
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
    if(lwip_fd(fd)) {
        uint32_t capacity;
        int rc=lwip_name_preflight(address,length_ptr,&capacity);
        if(rc)return (uint64_t)(int64_t)rc;
        rc=nas_lwip_socket_getname_begin(TASK.fds->entries[fd].slot,peer);
        return lwip_control_result(rc);
    }
    return (uint64_t)-EBADF;
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
    if(lwip_fd(fd)) {
        uint32_t length;
        if(!size_ptr || !nas_user_read_bytes(size_ptr,(uint8_t *)&length,4))
            return (uint64_t)-EFAULT;
        if(level==1 && opt==25) { /* SO_BINDTODEVICE: physical NIC name */
            int index=nas_lwip_socket_bound_ifindex(
                TASK.fds->entries[fd].slot);
            if(index<0)return (uint64_t)(int64_t)index;
            char name[NAS_LWIP_DEVICE_NAME_MAX]={0};
            uint32_t actual=0;
            if(index) {
                int rc=nas_lwip_guest_ifname((unsigned)index,name);
                if(rc)return (uint64_t)(int64_t)rc;
                actual=(uint32_t)strnlen(name,sizeof(name))+1U;
                if(actual>sizeof(name))return (uint64_t)-EIO;
            }
            if(length<actual)return (uint64_t)-EINVAL;
            if((actual && !nas_user_prepare_write(address,actual)) ||
               !nas_user_prepare_write(size_ptr,4))return (uint64_t)-EFAULT;
            return (!actual || nas_user_write_bytes(
                       address,(const uint8_t *)name,actual)) &&
                   nas_user_write_bytes(size_ptr,
                                        (const uint8_t *)&actual,4) ?
                   0:(uint64_t)-EFAULT;
        }
        if(length<4)return (uint64_t)-EINVAL;
        unsigned slot=TASK.fds->entries[fd].slot;
        int type=nas_lwip_socket_type(slot);
        if(type<0)return (uint64_t)(int64_t)type;
        uint32_t value;
        if(level==0 && (opt==11 || opt==8)) { /* IP_RECVERR/IP_PKTINFO */
            bool enabled=false;
            int rc=opt==8 ? nas_lwip_socket_ip_pktinfo(slot,false,&enabled) :
                             nas_lwip_socket_ip_recverr(slot,false,&enabled);
            if(rc)return (uint64_t)(int64_t)rc;
            value=enabled?1U:0U;
        } else if(level==6 && opt==1) { /* TCP_NODELAY */
            bool enabled=false;
            int rc=nas_lwip_socket_tcp_nodelay(slot,false,&enabled);
            if(rc)return (uint64_t)(int64_t)rc;
            value=enabled?1U:0U;
        } else if(level==1 && opt==9) { /* SO_KEEPALIVE */
            bool enabled=false;
            int rc=nas_lwip_socket_keepalive(slot,false,&enabled);
            if(rc)return (uint64_t)(int64_t)rc;
            value=enabled?1U:0U;
        } else if(level==1) {
            switch(opt) {
            case 3:value=type==NAS_LWIP_BRIDGE_TCP ? 1U:2U;break;
            case 4: {
                int error=nas_lwip_socket_port_take_error(slot);
                value=(uint32_t)(error<0?-error:error);break;
            }
            case 7: /* SO_SNDBUF: actual configured upstream TCP queue */
            case 8: { /* SO_RCVBUF: actual upstream TCP window */
                int capacity=nas_lwip_socket_buffer_capacity(slot,opt==7);
                if(capacity<0)return (uint64_t)(int64_t)capacity;
                value=(uint32_t)capacity;break;
            }
            case 30:value=nas_lwip_socket_is_listening(slot)?1U:0U;break;
            case 38:value=type==NAS_LWIP_BRIDGE_TCP ? 6U:17U;break;
            case 39:value=2U;break;
            default:return (uint64_t)-ENOPROTOOPT;
            }
        } else return (uint64_t)-ENOPROTOOPT;
        uint32_t actual=4;
        if(!nas_user_prepare_write(address,4) ||
           !nas_user_prepare_write(size_ptr,4))return (uint64_t)-EFAULT;
        return nas_user_write_bytes(address,(const uint8_t *)&value,4) &&
               nas_user_write_bytes(size_ptr,(const uint8_t *)&actual,4) ?
               0:(uint64_t)-EFAULT;
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_ipv4_setsockopt(uint64_t fd,uint64_t level,
                                    uint64_t opt,uint64_t address,
                                    uint64_t length) {
    if(lwip_fd(fd)) {
        if(level==1 && opt==25) { /* Linux SO_BINDTODEVICE = 25 */
            if(!address)return (uint64_t)-EFAULT;
            if(!length || length>NAS_LWIP_DEVICE_NAME_MAX)
                return (uint64_t)-EINVAL;
            char name[NAS_LWIP_DEVICE_NAME_MAX]={0};
            if(!nas_user_read_bytes(address,(uint8_t *)name,length))
                return (uint64_t)-EFAULT;
            unsigned namelen=(unsigned)strnlen(name,sizeof(name));
            if(namelen==sizeof(name))return (uint64_t)-ENAMETOOLONG;
            /* An empty name is Linux's request to clear the binding.
             * For a genuinely unbound socket it is already satisfied,
             * including Nping/libnsock's initial empty-device request.
             * Upstream Netconn has no API to clear an existing PCB bind;
             * reject that case instead of claiming a phantom unbind. */
            if(!namelen) {
                int current=nas_lwip_socket_bound_ifindex(
                    TASK.fds->entries[fd].slot);
                if(current<0)return (uint64_t)(int64_t)current;
                return current ? (uint64_t)-EOPNOTSUPP:0;
            }
            int index=nas_lwip_guest_ifindex(name);
            if(index<0)return (uint64_t)(int64_t)index;
            return lwip_control_result(nas_lwip_socket_bind_if(
                TASK.fds->entries[fd].slot,(unsigned)index));
        }
        if((level!=1 || (opt!=2 && opt!=6 && opt!=9)) &&
           (level!=0 || (opt!=11 && opt!=8)) &&
           (level!=6 || opt!=1))return (uint64_t)-ENOPROTOOPT;
        if(length<4 || !address)return (uint64_t)-EINVAL;
        uint32_t value;
        if(!nas_user_read_bytes(address,(uint8_t *)&value,4))
            return (uint64_t)-EFAULT;
        unsigned slot=TASK.fds->entries[fd].slot;
        if(level==0 && opt==8) { /* IP_PKTINFO: real Netbuf destination */
            bool enabled=value!=0;
            return lwip_control_result(nas_lwip_socket_ip_pktinfo(
                slot,true,&enabled));
        }
        if(level==0) { /* IP_RECVERR: per-OpenFile opt-in, no fake errqueue */
            bool enabled=value!=0;
            return (uint64_t)(int64_t)nas_lwip_socket_ip_recverr(
                slot,true,&enabled);
        }
        if(level==6) { /* TCP_NODELAY: real PCB Nagle flag */
            bool enabled=value!=0;
            int rc=nas_lwip_socket_tcp_nodelay(slot,true,&enabled);
            /* Async nonblocking CONNECT already owns the native command.
             * A separately held deferred option must not park on its
             * consumable connect completion or claim a phantom EBUSY. */
            return rc==1 ? 0 : lwip_control_result(rc);
        }
        if(opt==9) { /* SO_KEEPALIVE: real TCP probe-enable PCB flag */
            bool enabled=value!=0;
            return lwip_control_result(nas_lwip_socket_keepalive(
                slot,true,&enabled));
        }
        if(opt==6) {
            if(nas_lwip_socket_type(slot)!=NAS_LWIP_BRIDGE_UDP)
                return (uint64_t)-EOPNOTSUPP;
            return lwip_control_result(nas_lwip_socket_setbroadcast(
                slot,value!=0));
        }
        return lwip_control_result(nas_lwip_socket_setreuseaddr(slot,value!=0));
    }
    return (uint64_t)-EBADF;
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
/* AF_NETLINK exposes a kernel-owned ROUTE notification/query File, not a
 * second TCP/IP stack. This syscall adapter is the sole owner of usercopy,
 * iovec import, and Linux sockaddr_nl/msghdr layout on all three LP64 ISAs. */
static uint64_t sys_netlink_bind(uint64_t fd,uint64_t address,uint64_t length) {
    if(!netlink_fd(fd))return (uint64_t)-EBADF;
    if(length<NAS_NETLINK_ROUTE_SOCKADDR_SIZE)return (uint64_t)-EINVAL;
    uint8_t local[NAS_NETLINK_ROUTE_SOCKADDR_SIZE];
    if(!nas_user_read_bytes(address,local,sizeof(local)))
        return (uint64_t)-EFAULT;
    return (uint64_t)(int64_t)nas_netlink_route_bind(
        TASK.fds->entries[fd].slot,local,sizeof(local));
}
static uint64_t sys_netlink_name(uint64_t fd,uint64_t address,
                                  uint64_t length_ptr) {
    if(!netlink_fd(fd))return (uint64_t)-EBADF;
    if(!address || !length_ptr)return (uint64_t)-EFAULT;
    uint32_t capacity=0;
    if(!nas_user_read_bytes(length_ptr,(uint8_t *)&capacity,sizeof(capacity)))
        return (uint64_t)-EFAULT;
    uint8_t name[NAS_NETLINK_ROUTE_SOCKADDR_SIZE];
    int rc=nas_netlink_route_getsockname(TASK.fds->entries[fd].slot,name);
    if(rc)return (uint64_t)(int64_t)rc;
    uint32_t actual=sizeof(name),n=capacity<actual?capacity:actual;
    if(!nas_user_prepare_write(length_ptr,4) ||
       (n && !nas_user_prepare_write(address,n)))return (uint64_t)-EFAULT;
    return nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4) &&
           (!n || nas_user_write_bytes(address,name,n)) ?
           0:(uint64_t)-EFAULT;
}
static uint64_t sys_netlink_sendto(uint64_t fd,uint64_t user_data,
                                   uint64_t length,uint64_t flags,
                                   uint64_t user_name,uint64_t name_length) {
    if(!netlink_fd(fd))return (uint64_t)-EBADF;
    if(flags&~0x40ULL)return (uint64_t)-EOPNOTSUPP; /* MSG_DONTWAIT */
    if(length>NAS_NETLINK_ROUTE_MAX_DATAGRAM)return (uint64_t)-EMSGSIZE;
    if(user_name && name_length<NAS_NETLINK_ROUTE_SOCKADDR_SIZE)
        return (uint64_t)-EINVAL;
    if(!user_name && name_length)return (uint64_t)-EINVAL;
    uint8_t packet[NAS_NETLINK_ROUTE_MAX_DATAGRAM];
    uint8_t name[NAS_NETLINK_ROUTE_SOCKADDR_SIZE];
    if(length && !nas_user_read_bytes(user_data,packet,length))
        return (uint64_t)-EFAULT;
    if(user_name && !nas_user_read_bytes(user_name,name,sizeof(name)))
        return (uint64_t)-EFAULT;
    return (uint64_t)nas_netlink_route_sendto(TASK.fds->entries[fd].slot,
        packet,(unsigned)length,user_name?name:NULL,
        user_name?sizeof(name):0);
}
static uint64_t sys_netlink_recvmsg(uint64_t fd,uint64_t user_message,
                                    uint64_t flags) {
    if(!netlink_fd(fd))return (uint64_t)-EBADF;
    if(flags&~(0x2ULL|0x20ULL|0x40ULL))return (uint64_t)-EOPNOTSUPP;
    struct nas_msghdr msg;
    if(!nas_user_read_bytes(user_message,(uint8_t *)&msg,sizeof(msg)) ||
       !nas_user_prepare_write(user_message,sizeof(msg)))
        return (uint64_t)-EFAULT;
    if(msg.iovlen>32)return (uint64_t)-EMSGSIZE;
    struct nas_iovec iov[32];
    uint64_t capacity=0;
    for(unsigned i=0;i<msg.iovlen;i++) {
        if(msg.iov>UINT64_MAX-(uint64_t)i*sizeof(iov[i]) ||
           !nas_user_read_bytes(msg.iov+(uint64_t)i*sizeof(iov[i]),
                                (uint8_t *)&iov[i],sizeof(iov[i])))
            return (uint64_t)-EFAULT;
        uint64_t room=NAS_NETLINK_ROUTE_MAX_DATAGRAM-capacity;
        if(iov[i].length<room)room=iov[i].length;
        if(room && !nas_user_prepare_write(iov[i].base,room))
            return (uint64_t)-EFAULT;
        capacity+=room;
    }
    const uint32_t name_space=msg.namelen>NAS_NETLINK_ROUTE_SOCKADDR_SIZE ?
                              NAS_NETLINK_ROUTE_SOCKADDR_SIZE:msg.namelen;
    if(msg.name && name_space &&
       !nas_user_prepare_write(msg.name,name_space))return (uint64_t)-EFAULT;
    uint8_t packet[NAS_NETLINK_ROUTE_MAX_DATAGRAM];
    uint8_t source[NAS_NETLINK_ROUTE_SOCKADDR_SIZE];
    uint32_t source_length=0,received_flags=0;
    int64_t got=nas_netlink_route_recvmsg(TASK.fds->entries[fd].slot,
                   packet,(unsigned)capacity,(unsigned)flags,
                   source,&source_length,&received_flags);
    if(got<0)return (uint64_t)got;
    /* MSG_TRUNC as an input reports the datagram's ORIGINAL length, even
     * though only the user-requested prefix is copied. PEEK remains owned
     * by the kernel queue until the later consuming recvmsg. */
    uint64_t copied=(uint64_t)got<capacity?(uint64_t)got:capacity;
    uint64_t offset=0;
    for(unsigned i=0;i<msg.iovlen && offset<copied;i++) {
        uint64_t n=iov[i].length;
        if(n>copied-offset)n=copied-offset;
        if(n && !nas_user_write_bytes(iov[i].base,packet+offset,n))
            return (uint64_t)-EFAULT;
        offset+=n;
    }
    if(msg.name) {
        uint32_t n=name_space<source_length?name_space:source_length;
        if(n && !nas_user_write_bytes(msg.name,source,n))
            return (uint64_t)-EFAULT;
        msg.namelen=source_length;
    } else msg.namelen=0;
    msg.controllen=0;
    msg.flags=received_flags;
    return nas_user_write_bytes(user_message,(const uint8_t *)&msg,
                                sizeof(msg)) ? (uint64_t)got:(uint64_t)-EFAULT;
}
static uint64_t sys_netlink_getsockopt(uint64_t fd,uint64_t level,
                                       uint64_t option,uint64_t value,
                                       uint64_t length_ptr) {
    if(!netlink_fd(fd))return (uint64_t)-EBADF;
    if(level!=1)return (uint64_t)-ENOPROTOOPT; /* SOL_SOCKET */
    uint32_t answer;
    switch(option) {
    case 3:answer=3;break;  /* SO_TYPE: SOCK_RAW */
    case 4:answer=0;break;  /* SO_ERROR: synchronous kernel route endpoint */
    case 38:answer=0;break; /* SO_PROTOCOL: NETLINK_ROUTE */
    case 39:answer=16;break;/* SO_DOMAIN: AF_NETLINK */
    default:return (uint64_t)-ENOPROTOOPT;
    }
    uint32_t length;
    if(!nas_user_read_bytes(length_ptr,(uint8_t *)&length,4))
        return (uint64_t)-EFAULT;
    if(length<4)return (uint64_t)-EINVAL;
    const uint32_t actual=4;
    if(!nas_user_prepare_write(value,4) ||
       !nas_user_prepare_write(length_ptr,4))return (uint64_t)-EFAULT;
    return nas_user_write_bytes(value,(const uint8_t *)&answer,4) &&
           nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4) ?
           0:(uint64_t)-EFAULT;
}
/* Linux LP64 cmsghdr (16 bytes) + struct in_pktinfo (12 bytes).
 * IP addresses are byte strings in network order, unlike the native
 * interface index and cmsg fields. CMSG_SPACE(12)=32, CMSG_LEN(12)=28. */
static void ipv4_pktinfo_cmsg(uint8_t out[32],
                              struct nas_lwip_bridge_pktinfo info) {
    memset(out,0,32);
    const uint64_t cmsg_len=28;
    const uint32_t level=0,type=8;
    memcpy(out,&cmsg_len,8);
    memcpy(out+8,&level,4);memcpy(out+12,&type,4);
    memcpy(out+16,&info.ifindex,4);
    const uint32_t ip[2]={info.spec_dst,info.destination};
    for(unsigned i=0;i<2;i++)for(unsigned b=0;b<4;b++)
        out[20+4*i+b]=(uint8_t)(ip[i]>>(24-8*b));
}
static int ipv4_pktinfo_import(const struct nas_msghdr *msg,
                               struct nas_lwip_bridge_pktinfo *info) {
    if(!msg->controllen)return 0;
    if(!msg->control)return -EFAULT;
    if(msg->controllen<28 || msg->controllen>32)return -EOPNOTSUPP;
    uint8_t bytes[32]={0};
    if(!nas_user_read_bytes(msg->control,bytes,msg->controllen))return -EFAULT;
    uint64_t cmsg_len;
    uint32_t level,type;
    memcpy(&cmsg_len,bytes,8);memcpy(&level,bytes+8,4);
    memcpy(&type,bytes+12,4);
    if(cmsg_len!=28 || level!=0 || type!=8)return -EOPNOTSUPP;
    /* Trailing CMSG_ALIGN padding is unspecified userspace memory. Its
     * contents have no protocol meaning and are not a second header. */
    memcpy(&info->ifindex,bytes+16,4);
    info->spec_dst=0;info->destination=0;
    for(unsigned b=0;b<4;b++) {
        info->spec_dst=info->spec_dst<<8|bytes[20+b];
        info->destination=info->destination<<8|bytes[24+b];
    }
    return 1;
}
int nas_fs_socket_tcp_sendmsg_iov(uint64_t fd,uint64_t address,
                                 uint64_t flags,uint64_t *iov,
                                 uint64_t *count) {
    if(!lwip_fd(fd) || nas_lwip_socket_type(TASK.fds->entries[fd].slot)!=
       NAS_LWIP_BRIDGE_TCP)return 0;
    if(!iov || !count)return -EINVAL;
    struct nas_msghdr msg;
    if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)))return -EFAULT;
    if(flags & ~0x4000ULL)return -EOPNOTSUPP; /* only MSG_NOSIGNAL */
    if(msg.name)return -EISCONN;
    if(msg.controllen)return -EOPNOTSUPP;
    /* writev's shared importer validates every iovec before data transfer
     * and pins the copied vector across Task parking and FD reuse. */
    *iov=msg.iov;
    *count=msg.iovlen;
    return 1;
}
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
    if(lwip_fd(fd)) {
        unsigned slot=TASK.fds->entries[fd].slot;
        struct nas_msghdr msg;
        if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)))
            return (uint64_t)-EFAULT;
        if(nas_lwip_socket_type(slot)==NAS_LWIP_BRIDGE_TCP) {
            /* Linux sendmsg() on connected TCP is a scatter/gather stream
             * write, not a datagram-only operation. Use the exact same
             * held-OpenFile vector writer as writev(): real Netconn TX,
             * bounds-checked guest iovecs and honest partial writes. The
             * per-call MSG_DONTWAIT override needs a separate I/O mode and
             * must not silently mutate shared O_NONBLOCK state. */
            uint64_t iov=0,count=0;
            int rc=nas_fs_socket_tcp_sendmsg_iov(fd,address,flags,&iov,&count);
            if(rc<0)return (uint64_t)(int64_t)rc;
            if(rc!=1)return (uint64_t)-EIO;
            return nas_fs_fd_writev(fd,iov,count);
        }
        if(nas_lwip_socket_type(slot)!=NAS_LWIP_BRIDGE_UDP)
            return (uint64_t)-EOPNOTSUPP;
        if(flags & ~(0x4000ULL|0x40ULL))return (uint64_t)-EOPNOTSUPP;
        struct nas_lwip_bridge_pktinfo pktinfo={0};
        int ancillary=ipv4_pktinfo_import(&msg,&pktinfo);
        if(ancillary<0)return (uint64_t)(int64_t)ancillary;
        if(msg.iovlen>32)return (uint64_t)-EMSGSIZE;
        struct nas_lwip_bridge_address destination={0};
        if(msg.name) {
            struct nas_ipv4_endpoint ep;
            int rc=ipv4_endpoint(msg.name,msg.namelen,&ep);
            if(rc)return (uint64_t)(int64_t)rc;
            destination=lwip_address(ep);
        }
        uint8_t packet[NAS_LWIP_BRIDGE_DATA];
        size_t total=0;
        for(unsigned i=0;i<msg.iovlen;i++) {
            struct nas_iovec v;
            if(msg.iov>UINT64_MAX-(uint64_t)i*sizeof(v) ||
               !nas_user_read_bytes(msg.iov+(uint64_t)i*sizeof(v),
                                    (uint8_t *)&v,sizeof(v)))
                return (uint64_t)-EFAULT;
            if(v.length>NAS_LWIP_BRIDGE_DATA-total)
                return (uint64_t)-EMSGSIZE;
            if(v.length && !nas_user_read_bytes(v.base,packet+total,v.length))
                return (uint64_t)-EFAULT;
            total+=(size_t)v.length;
        }
        return (uint64_t)nas_lwip_socket_sendto_pktinfo(slot,packet,
                    (uint16_t)total,destination,
                    ancillary ? &pktinfo:NULL);
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_ipv4_recvmsg(uint64_t fd,uint64_t address,uint64_t flags) {
    if(lwip_fd(fd)) {
        unsigned slot=TASK.fds->entries[fd].slot;
        if(nas_lwip_socket_type(slot)!=NAS_LWIP_BRIDGE_UDP)
            return (uint64_t)-EOPNOTSUPP;
        if(flags & ~(0x40ULL|0x4000ULL))return (uint64_t)-EOPNOTSUPP;
        struct nas_msghdr msg;
        if(!nas_user_read_bytes(address,(uint8_t *)&msg,sizeof(msg)) ||
           !nas_user_prepare_write(address,sizeof(msg)))
            return (uint64_t)-EFAULT;
        if(msg.iovlen>32)return (uint64_t)-EMSGSIZE;
        struct nas_iovec iov[32];
        uint64_t capacity=0;
        for(unsigned i=0;i<msg.iovlen;i++) {
            if(msg.iov>UINT64_MAX-(uint64_t)i*sizeof(iov[i]) ||
               !nas_user_read_bytes(msg.iov+(uint64_t)i*sizeof(iov[i]),
                                    (uint8_t *)&iov[i],sizeof(iov[i])))
                return (uint64_t)-EFAULT;
            uint64_t budget=NAS_LWIP_BRIDGE_DATA-capacity;
            if(iov[i].length<budget)budget=iov[i].length;
            if(budget && !nas_user_prepare_write(iov[i].base,budget))
                return (uint64_t)-EFAULT;
            capacity+=budget;
        }
        unsigned name_capacity=msg.namelen>16?16U:msg.namelen;
        if(msg.name && name_capacity &&
           !nas_user_prepare_write(msg.name,name_capacity))
            return (uint64_t)-EFAULT;
        bool enabled=false;
        int option=nas_lwip_socket_ip_pktinfo(slot,false,&enabled);
        if(option)return (uint64_t)(int64_t)option;
        /* Preflight ancillary output BEFORE consuming the datagram. */
        unsigned control_bytes=enabled && msg.control && msg.controllen>=28 ?
                               (msg.controllen>=32 ? 32U:28U):0U;
        if(control_bytes && !nas_user_prepare_write(msg.control,control_bytes))
            return (uint64_t)-EFAULT;
        uint8_t packet[NAS_LWIP_BRIDGE_DATA];
        struct nas_lwip_bridge_address source={0};
        struct nas_lwip_bridge_pktinfo info={0};
        bool truncated=false;
        int64_t received=nas_lwip_socket_recvfrom(slot,packet,
            (uint16_t)capacity,&source,&truncated,&info);
        if(received<0)return (uint64_t)received;
        unsigned offset=0;
        for(unsigned i=0;i<msg.iovlen && offset<(unsigned)received;i++) {
            uint64_t count=iov[i].length;
            if(count>(uint64_t)received-offset)count=(uint64_t)received-offset;
            if(count && !nas_user_write_bytes(iov[i].base,packet+offset,count))
                return (uint64_t)-EFAULT;
            offset+=(unsigned)count;
        }
        if(msg.name) {
            uint8_t bytes[16];
            ipv4_sockaddr((struct nas_ipv4_endpoint){source.ipv4,
                          source.port},bytes);
            if(name_capacity && !nas_user_write_bytes(
                msg.name,bytes,name_capacity))return (uint64_t)-EFAULT;
            msg.namelen=16;
        } else msg.namelen=0;
        if(control_bytes) {
            /* A configured option must report actual inbound metadata;
             * never construct fictitious destination or interface info. */
            if(!info.ifindex || !info.destination)return (uint64_t)-EIO;
            uint8_t control[32];
            ipv4_pktinfo_cmsg(control,info);
            if(!nas_user_write_bytes(msg.control,control,control_bytes))
                return (uint64_t)-EFAULT;
        }
        msg.controllen=control_bytes;
        msg.flags=(truncated?0x20U:0U) |
                  (enabled && !control_bytes ? 0x8U:0U); /* MSG_CTRUNC */
        return nas_user_write_bytes(address,(const uint8_t *)&msg,sizeof(msg)) ?
               (uint64_t)received:(uint64_t)-EFAULT;
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_ipv4_sendto(uint64_t fd,uint64_t buffer,
                                uint64_t length,uint64_t flags,
                                uint64_t destination,uint64_t address_size) {
    if(lwip_fd(fd)) {
        /* Connected TCP/UDP goes through the existing parked FileOps path.
         * Connectionless UDP is copied as ONE kernel-owned datagram with
         * its actual destination, never split into stream writes. */
        /* Linux MSG_MORE on TCP is a packet-coalescing hint, NOT an
         * additional stream boundary. The upstream Netconn TX queue may
         * flush promptly; preserve every accepted byte instead of rejecting
         * legitimate HAProxy proxy replies with EOPNOTSUPP. This does not
         * implement UDP MSG_MORE corking (which changes datagram framing). */
        unsigned slot=TASK.fds->entries[fd].slot;
        const bool tcp=nas_lwip_socket_type(slot)==NAS_LWIP_BRIDGE_TCP;
        if(flags & ~(0x40ULL|0x4000ULL|(tcp?0x8000ULL:0ULL)))
            return (uint64_t)-EOPNOTSUPP;
        if(destination) {
            if(nas_lwip_socket_type(slot)!=NAS_LWIP_BRIDGE_UDP)
                return (uint64_t)-EISCONN;
            struct nas_ipv4_endpoint ep;
            int rc=ipv4_endpoint(destination,address_size,&ep);
            if(rc)return (uint64_t)(int64_t)rc;
            if(length>NAS_LWIP_BRIDGE_DATA)return (uint64_t)-EMSGSIZE;
            uint8_t packet[NAS_LWIP_BRIDGE_DATA];
            if(length && !nas_user_read_bytes(buffer,packet,length))
                return (uint64_t)-EFAULT;
            return (uint64_t)nas_lwip_socket_sendto(slot,packet,
                (uint16_t)length,lwip_address(ep));
        }
        if(address_size)return (uint64_t)-EINVAL;
        return nas_fs_fd_write(fd,buffer,length);
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_ipv4_recvfrom(uint64_t fd,uint64_t buffer,
                                  uint64_t length,uint64_t flags,
                                  uint64_t address,uint64_t length_ptr) {
    if(lwip_fd(fd)) {
        unsigned slot=TASK.fds->entries[fd].slot;
        if(nas_lwip_socket_type(slot)==NAS_LWIP_BRIDGE_UDP) {
            if(flags & ~(0x40ULL|0x4000ULL))return (uint64_t)-EOPNOTSUPP;
            if(address && !length_ptr)return (uint64_t)-EFAULT;
            uint32_t capacity=0;
            if(address) {
                if(!nas_user_read_bytes(length_ptr,(uint8_t *)&capacity,4))
                    return (uint64_t)-EFAULT;
                unsigned n=capacity>16?16U:capacity;
                if(!nas_user_prepare_write(length_ptr,4) ||
                   (n && !nas_user_prepare_write(address,n)))
                    return (uint64_t)-EFAULT;
            }
            uint64_t available=length>NAS_LWIP_BRIDGE_DATA ?
                               NAS_LWIP_BRIDGE_DATA:length;
            if(available && !nas_user_prepare_write(buffer,available))
                return (uint64_t)-EFAULT;
            uint8_t packet[NAS_LWIP_BRIDGE_DATA];
            struct nas_lwip_bridge_address source={0};
            int64_t received=nas_lwip_socket_recvfrom(slot,packet,
                (uint16_t)available,&source,NULL,NULL);
            if(received<0)return (uint64_t)received;
            if(received && !nas_user_write_bytes(buffer,packet,
                                                  (size_t)received))
                return (uint64_t)-EFAULT;
            if(address) {
                uint8_t bytes[16];
                ipv4_sockaddr((struct nas_ipv4_endpoint){
                    source.ipv4,source.port},bytes);
                unsigned n=capacity>16?16U:capacity;
                const uint32_t actual=16;
                if((n && !nas_user_write_bytes(address,bytes,n)) ||
                   !nas_user_write_bytes(length_ptr,
                                         (const uint8_t *)&actual,4))
                    return (uint64_t)-EFAULT;
            }
            return (uint64_t)received;
        }
        /* Pure MSG_PEEK is a non-consuming TCP FileOps read. Mixed flags
         * require separate per-call O_NONBLOCK semantics, not silent ignore. */
        if(flags && flags!=0x2ULL)return (uint64_t)-EOPNOTSUPP;
        if(address && !length_ptr)return (uint64_t)-EFAULT;
        uint32_t capacity=0;
        uint8_t peer_bytes[16];
        if(address) {
            /* A connected TCP recvfrom may legitimately request its peer
             * sockaddr. The native worker/accept path already captured that
             * endpoint in the OpenFile; never use the fake-loopback state or
             * call Netconn from this userspace syscall. UDP requires true
             * per-datagram RX metadata and stays explicitly unsupported. */
            struct nas_lwip_bridge_address peer={0};
            int rc=nas_lwip_socket_peer_snapshot(
                TASK.fds->entries[fd].slot,&peer);
            if(rc)return (uint64_t)(int64_t)rc;
            if(!nas_user_read_bytes(length_ptr,(uint8_t *)&capacity,4))
                return (uint64_t)-EFAULT;
            unsigned n=capacity>16?16U:capacity;
            if(!nas_user_prepare_write(length_ptr,4) ||
               (n && !nas_user_prepare_write(address,n)))
                return (uint64_t)-EFAULT;
            ipv4_sockaddr((struct nas_ipv4_endpoint){peer.ipv4,peer.port},
                          peer_bytes);
        }
        uint64_t received=(flags&0x2ULL) ?
            nas_fs_fd_peek(fd,buffer,length) :
            nas_fs_fd_read(fd,buffer,length);
        if((int64_t)received<0)return received;
        if(address) {
            const uint32_t actual=16;
            unsigned n=capacity>16?16U:capacity;
            if((n && !nas_user_write_bytes(address,peer_bytes,n)) ||
               !nas_user_write_bytes(length_ptr,(const uint8_t *)&actual,4))
                return (uint64_t)-EFAULT;
        }
        return received;
    }
    return (uint64_t)-EBADF;
}
static uint64_t sys_socket_shutdown(uint64_t fd,uint64_t how) {
    if(!unix_fd(fd) && !inet_fd(fd))return (uint64_t)-EBADF;
    if(how>2ULL)return (uint64_t)-EINVAL;
    if(lwip_fd(fd))return lwip_control_result(nas_lwip_socket_shutdown(
        TASK.fds->entries[fd].slot,(unsigned)how));
    unsigned slot=TASK.fds->entries[fd].slot;
    return (uint64_t)(int64_t)nas_pipe_socket_shutdown(slot,(unsigned)how);
}

/* A valid pipe/file/TTY FD is not a socket: Linux reports ENOTSOCK, whereas
 * an unallocated/closed FD reports EBADF. Check the whole socket syscall
 * family before protocol routing; Ncat probes piped stdin with recvfrom(). */
static int socket_fd_error(enum nas_syscall_op op,uint64_t fd) {
    switch (op) {
    case NAS_SC_bind: case NAS_SC_listen: case NAS_SC_connect:
    case NAS_SC_accept: case NAS_SC_accept4:
    case NAS_SC_getsockname: case NAS_SC_getpeername:
    case NAS_SC_getsockopt: case NAS_SC_setsockopt:
    case NAS_SC_sendmsg: case NAS_SC_recvmsg:
    case NAS_SC_sendto: case NAS_SC_recvfrom: case NAS_SC_shutdown:
        if(fd>=NAS_FD_LIMIT || TASK.fds->entries[fd].kind==FD_CLOSED)
            return -EBADF;
        return TASK.fds->entries[fd].kind==FD_SOCKET ? 0:-ENOTSOCK;
    default: return 0;
    }
}
#define SOCKET_RESULT(expr) return (struct nas_fs_result){ .handled=true, .value=(expr) }
struct nas_fs_result nas_fs_socket_syscall(enum nas_syscall_op op,
                                         const uint64_t *args) {
    int fd_error=socket_fd_error(op,args[1]);
    if(fd_error)SOCKET_RESULT((uint64_t)(int64_t)fd_error);
    if(op!=NAS_SC_socket && netlink_fd(args[1])) {
        switch(op) {
        case NAS_SC_bind:
            SOCKET_RESULT(sys_netlink_bind(args[1],args[2],args[3]));
        case NAS_SC_getsockname:
            SOCKET_RESULT(sys_netlink_name(args[1],args[2],args[3]));
        case NAS_SC_getsockopt:
            SOCKET_RESULT(sys_netlink_getsockopt(args[1],args[2],args[3],
                                                  args[4],args[5]));
        case NAS_SC_sendto:
            SOCKET_RESULT(sys_netlink_sendto(args[1],args[2],args[3],
                           args[4],args[5],args[6]));
        case NAS_SC_recvmsg:
            SOCKET_RESULT(sys_netlink_recvmsg(args[1],args[2],args[3]));
        case NAS_SC_getpeername:SOCKET_RESULT((uint64_t)-ENOTCONN);
        case NAS_SC_connect:case NAS_SC_listen:
        case NAS_SC_accept:case NAS_SC_accept4:
        case NAS_SC_setsockopt:case NAS_SC_sendmsg:
        case NAS_SC_recvfrom:case NAS_SC_shutdown:
            SOCKET_RESULT((uint64_t)-EOPNOTSUPP);
        default:break;
        }
    }
    switch (op) {
    case NAS_SC_socket: SOCKET_RESULT(sys_socket(args[1],args[2],args[3]));
    case NAS_SC_bind: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_bind(args[1],args[2],args[3]) :
        sys_unix_bind(args[1],args[2],args[3]));
    case NAS_SC_listen: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_listen(args[1],args[2]) : sys_unix_listen(args[1],args[2]));
    case NAS_SC_connect: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_connect(args[1],args[2],args[3]) :
        sys_unix_connect(args[1],args[2],args[3]));
    case NAS_SC_accept: SOCKET_RESULT(sys_socket_accept_fd(
        args[1],args[2],args[3],0));
    case NAS_SC_accept4: SOCKET_RESULT(sys_socket_accept_fd(
        args[1],args[2],args[3],args[4]));
    case NAS_SC_getsockname: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_name(args[1],args[2],args[3],false) :
        sys_unix_name(args[1],args[2],args[3],false));
    case NAS_SC_getpeername: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_name(args[1],args[2],args[3],true) :
        sys_unix_name(args[1],args[2],args[3],true));
    case NAS_SC_getsockopt:
        SOCKET_RESULT(inet_fd(args[1]) ?
            sys_ipv4_getsockopt(args[1],args[2],args[3],args[4],args[5]) :
            sys_unix_getsockopt(args[1],args[2],args[3],args[4],args[5]));
    case NAS_SC_setsockopt: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_setsockopt(args[1],args[2],args[3],args[4],args[5]) :
        (uint64_t)-ENOPROTOOPT);
    case NAS_SC_sendmsg: SOCKET_RESULT(inet_fd(args[1]) ?
        sys_ipv4_sendmsg(args[1],args[2],args[3]) :
        sys_unix_sendmsg(args[1],args[2],args[3]));
    case NAS_SC_recvmsg: SOCKET_RESULT(inet_fd(args[1]) ?
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
