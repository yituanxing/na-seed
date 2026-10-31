/* Local IPv4 TCP stream transport. This is intentionally a loopback-only
 * networking foundation: 127.0.0.1 connects to in-kernel ports and carries
 * real bidirectional bytes through owned stream rings. It cannot reach a NIC,
 * route external IP packets, speak UDP, or claim a full TCP/IP wire stack.
 * A syscall adapter MUST reject those unsupported requests explicitly. */
#include "ipv4_loopback.h"
#include "../fd.h"
#include "../pipe.h"
#include "../errno.h"
#include "../fs/file_ops.h"
#include "../fs/syscall.h"

#define NAS_IPV4_BACKLOG 16U
#define NAS_IPV4_EPHEMERAL_MIN 32768U
#define NAS_IPV4_EPHEMERAL_MAX 60999U
enum ipv4_state { IPV4_FREE, IPV4_NEW, IPV4_BOUND, IPV4_LISTEN, IPV4_CONNECTED };
struct ipv4_socket {
    enum ipv4_state state;
    bool reuseaddr;
    struct nas_ipv4_endpoint local,peer;
    bool has_local,has_peer;
    unsigned backlog,first,count,queue[NAS_IPV4_BACKLOG];
};
static struct ipv4_socket sockets[NAS_OPEN_FILE_LIMIT];
static unsigned ephemeral_next=NAS_IPV4_EPHEMERAL_MIN;

static bool valid_local(uint32_t address) {
    return address==NAS_IPV4_ANY || address==NAS_IPV4_LOOPBACK;
}
static bool port_conflict(unsigned except,struct nas_ipv4_endpoint candidate) {
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        const struct ipv4_socket *s=&sockets[i];
        if(i==except || !s->has_local || !s->local.port ||
           s->state==IPV4_FREE)continue;
        if(s->local.port==candidate.port &&
           (s->local.address==NAS_IPV4_ANY ||
            candidate.address==NAS_IPV4_ANY ||
            s->local.address==candidate.address)) {
            /* SO_REUSEADDR only shares the pre-listening bind namespace.
             * Connected streams and live listeners are never ambiguous. */
            if(sockets[except].reuseaddr && s->reuseaddr &&
               s->state==IPV4_BOUND)continue;
            return true;
        }
    }
    return false;
}
static int choose_ephemeral(unsigned slot,uint32_t address,uint16_t *port) {
    for(unsigned n=NAS_IPV4_EPHEMERAL_MIN;n<=NAS_IPV4_EPHEMERAL_MAX;n++) {
        unsigned next=ephemeral_next++;
        if(ephemeral_next>NAS_IPV4_EPHEMERAL_MAX)
            ephemeral_next=NAS_IPV4_EPHEMERAL_MIN;
        struct nas_ipv4_endpoint candidate={address,(uint16_t)next};
        if(!port_conflict(slot,candidate)) {
            *port=candidate.port;
            return 0;
        }
    }
    return -EADDRINUSE;
}

void nas_ipv4_socket_init(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT)
        sockets[slot]=(struct ipv4_socket){.state=IPV4_NEW};
}
bool nas_ipv4_socket_active(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].state!=IPV4_FREE;
}
void nas_ipv4_socket_release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    struct ipv4_socket *s=&sockets[slot];
    if(s->state==IPV4_FREE)return;
    /* Pending accepted descriptions belong to the listener, not an FD.
     * Remove that ownership before recursive open-description finalizers. */
    struct ipv4_socket old=*s;
    *s=(struct ipv4_socket){0};
    for(unsigned i=0;i<old.count;i++) {
        unsigned peer=old.queue[(old.first+i)%NAS_IPV4_BACKLOG];
        if(peer<NAS_OPEN_FILE_LIMIT)
            nas_fs_drop_kernel_description(peer);
    }
}
int nas_ipv4_socket_bind(unsigned slot,struct nas_ipv4_endpoint local) {
    if(!nas_ipv4_socket_active(slot))return -EBADF;
    if(sockets[slot].state!=IPV4_NEW)return -EINVAL;
    if(!valid_local(local.address))return -EADDRNOTAVAIL;
    if(!local.port) {
        int rc=choose_ephemeral(slot,local.address,&local.port);
        if(rc)return rc;
    } else if(port_conflict(slot,local))return -EADDRINUSE;
    sockets[slot].local=local;
    sockets[slot].has_local=true;
    sockets[slot].state=IPV4_BOUND;
    return 0;
}
int nas_ipv4_socket_listen(unsigned slot,unsigned backlog) {
    if(!nas_ipv4_socket_active(slot))return -EBADF;
    struct ipv4_socket *s=&sockets[slot];
    if(s->state==IPV4_NEW) {
        int rc=nas_ipv4_socket_bind(slot,
                    (struct nas_ipv4_endpoint){NAS_IPV4_ANY,0});
        if(rc)return rc;
    }
    if(s->state!=IPV4_BOUND && s->state!=IPV4_LISTEN)return -EINVAL;
    /* Two SO_REUSEADDR sockets may reserve the same port, but only one
     * may become a listener. Preserve connect's unique destination. */
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        if(i==slot || sockets[i].state!=IPV4_LISTEN)continue;
        const struct ipv4_socket *other=&sockets[i];
        if(other->local.port==s->local.port &&
           (other->local.address==s->local.address ||
            other->local.address==NAS_IPV4_ANY ||
            s->local.address==NAS_IPV4_ANY))return -EADDRINUSE;
    }
    /* Linux permits listen(fd,0): the kernel still reserves at least one
     * connection slot rather than reporting a fabricated failed listener. */
    s->backlog=backlog>NAS_IPV4_BACKLOG?NAS_IPV4_BACKLOG:backlog;
    if(!s->backlog)s->backlog=1;
    s->state=IPV4_LISTEN;
    return 0;
}
int nas_ipv4_socket_connect(unsigned slot,struct nas_ipv4_endpoint remote) {
    if(!nas_ipv4_socket_active(slot))return -EBADF;
    struct ipv4_socket *client=&sockets[slot];
    if(client->state==IPV4_CONNECTED)return -EISCONN;
    if(client->state==IPV4_LISTEN)return -EOPNOTSUPP;
    if(client->state!=IPV4_NEW && client->state!=IPV4_BOUND)return -EINVAL;
    if(remote.address!=NAS_IPV4_LOOPBACK)return -ENETUNREACH;
    if(!remote.port)return -ECONNREFUSED;
    if(client->state==IPV4_NEW) {
        int rc=nas_ipv4_socket_bind(slot,
                    (struct nas_ipv4_endpoint){NAS_IPV4_LOOPBACK,0});
        if(rc)return rc;
    } else if(client->local.address==NAS_IPV4_ANY) {
        client->local.address=NAS_IPV4_LOOPBACK;
    }
    unsigned listener=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        struct ipv4_socket *s=&sockets[i];
        if(s->state==IPV4_LISTEN && s->local.port==remote.port &&
           (s->local.address==NAS_IPV4_ANY ||
            s->local.address==remote.address)) {
            listener=i;
            break;
        }
    }
    if(listener==NAS_OPEN_FILE_LIMIT)return -ECONNREFUSED;
    struct ipv4_socket *server=&sockets[listener];
    if(server->count>=server->backlog)return -EAGAIN;
    unsigned pending=NAS_OPEN_FILE_LIMIT;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        if(i!=slot && !nas_open_files[i].refcount) {
            pending=i;
            break;
        }
    }
    if(pending==NAS_OPEN_FILE_LIMIT)return -ENFILE;
    int rc=nas_pipe_create_socketpair(slot,pending);
    if(rc)return rc;
    nas_open_files[pending]=(struct nas_open_file){
        .refcount=1,.status_flags=2,
        .ops=nas_fs_ops_for_kind(FD_SOCKET)
    };
    sockets[pending]=(struct ipv4_socket){
        .state=IPV4_CONNECTED,.has_local=true,.has_peer=true,
        .local={NAS_IPV4_LOOPBACK,server->local.port},
        .peer=client->local
    };
    client->state=IPV4_CONNECTED;
    client->peer=remote;
    client->has_peer=true;
    server->queue[(server->first+server->count)%NAS_IPV4_BACKLOG]=pending;
    server->count++;
    return 0;
}
int nas_ipv4_socket_accept(unsigned listener,unsigned *accepted) {
    if(!accepted || listener>=NAS_OPEN_FILE_LIMIT ||
       sockets[listener].state!=IPV4_LISTEN)return -EINVAL;
    struct ipv4_socket *s=&sockets[listener];
    if(!s->count)return -EAGAIN;
    *accepted=s->queue[s->first];
    s->first=(s->first+1U)%NAS_IPV4_BACKLOG;
    s->count--;
    return 0;
}
int nas_ipv4_socket_name(unsigned slot,bool peer,
                         struct nas_ipv4_endpoint *out) {
    if(!out || !nas_ipv4_socket_active(slot))return -EBADF;
    const struct ipv4_socket *s=&sockets[slot];
    if(peer && !s->has_peer)return -ENOTCONN;
    *out=peer?s->peer:s->local;
    return 0;
}
bool nas_ipv4_socket_listening(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].state==IPV4_LISTEN;
}
bool nas_ipv4_socket_readable(unsigned slot) {
    return nas_ipv4_socket_listening(slot) && sockets[slot].count!=0;
}
int nas_ipv4_socket_reuseaddr(unsigned slot,bool set,bool *value) {
    if(!value || !nas_ipv4_socket_active(slot))return -EBADF;
    if(set)sockets[slot].reuseaddr=*value;
    *value=sockets[slot].reuseaddr;
    return 0;
}
