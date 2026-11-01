/* Original lwIP Netconn owned by a NaSeed native kthread. No socket parser,
 * second TCP/IP stack, user address, FD namespace or blocking user syscall. */
#include "netconn_bridge.h"
#include "../../kernel/fd.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/task/k_sync.h"
#include "../../kernel/task/kthread.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include <stdint.h>
#include <stdbool.h>

_Static_assert(NAS_LWIP_BRIDGE_SLOTS==NAS_OPEN_FILE_LIMIT,
               "bridge identity must be one global open-description slot");

enum bridge_internal_op { BRIDGE_OPEN=100 };
struct bridge_slot {
    uint32_t generation;
    bool owned,closing,queued,executing,finished,accept_cancelled,listening;
    unsigned accept_pending;
    enum nas_lwip_bridge_type type;
    struct netconn *connection;
    struct netbuf *held_tcp;
    uint16_t held_offset;
    struct nas_lwip_bridge_request request;
    uint8_t copied_send[NAS_LWIP_BRIDGE_DATA];
    struct nas_lwip_bridge_result result;
};
static struct {
    struct bridge_slot slots[NAS_LWIP_BRIDGE_SLOTS];
    uint16_t queue[NAS_LWIP_BRIDGE_QUEUE];
    unsigned first,count,active;
    struct nas_ksem wake;
    struct nas_kthread_handle worker;
    bool started;
} bridge;

/* API_EVENT is fired by the official lwIP core after enqueueing an accepted
 * child (including a reported connection error), and by netconn_accept after
 * dequeue. It contains no Task/FD information and never calls the scheduler.
 * The bounded bridge registry maps the upstream Netconn back to its owner. */
static void netconn_event(struct netconn *connection,enum netconn_evt event,
                          u16_t length) {
    (void)length;
    for(unsigned i=0;i<NAS_LWIP_BRIDGE_SLOTS;i++) {
        struct bridge_slot *s=&bridge.slots[i];
        if(!s->owned || s->closing || s->connection!=connection)continue;
        if(event==NETCONN_EVT_RCVPLUS && s->accept_pending<UINT16_MAX)
            s->accept_pending++;
        else if(event==NETCONN_EVT_RCVMINUS && s->accept_pending)
            s->accept_pending--;
        return;
    }
}

static bool accept_lease_valid(unsigned slot,uint64_t token) {
    return slot<NAS_OPEN_FILE_LIMIT && token &&
           nas_open_files[slot].refcount==1 &&
           nas_open_files[slot].lease_token==token;
}

static struct bridge_slot *lookup(struct nas_lwip_bridge_id id) {
    if(id.slot>=NAS_LWIP_BRIDGE_SLOTS || !id.generation)return NULL;
    struct bridge_slot *s=&bridge.slots[id.slot];
    return s->owned && !s->closing && s->generation==id.generation ? s : NULL;
}
static struct nas_lwip_bridge_id reserve(unsigned slot,
                                         enum nas_lwip_bridge_type type) {
    struct nas_lwip_bridge_id none={0};
    if(slot>=NAS_LWIP_BRIDGE_SLOTS || bridge.active>=NAS_LWIP_BRIDGE_ACTIVE_LIMIT ||
       bridge.slots[slot].owned)return none;
    struct bridge_slot *s=&bridge.slots[slot];
    uint32_t generation=s->generation+1U;
    if(!generation)return none; /* no ABA after 2^32 incarnations */
    memset(s,0,sizeof(*s));
    s->generation=generation;
    s->owned=true;
    s->type=type;
    bridge.active++;
    return (struct nas_lwip_bridge_id){(uint16_t)slot,generation};
}
static bool enqueue(unsigned slot) {
    if(bridge.count==NAS_LWIP_BRIDGE_QUEUE)return false;
    bridge.queue[(bridge.first+bridge.count)%NAS_LWIP_BRIDGE_QUEUE]=(uint16_t)slot;
    bridge.count++;
    (void)nas_ksem_post(&bridge.wake);
    return true;
}
static void free_native(unsigned slot) {
    struct bridge_slot *s=&bridge.slots[slot];
    /* ACCEPT creates an owned Netconn before userspace can install a new
     * File. Until take() transfers the result, the LISTENER owns this child.
     * Closing the listener must not orphan an accepted, unclaimed handle. */
    if(s->finished && s->result.accepted.generation) {
        struct bridge_slot *child=lookup(s->result.accepted);
        if(child) {
            child->closing=true;
            (void)nas_ksem_post(&bridge.wake);
        }
    }
    if(s->held_tcp)netbuf_delete(s->held_tcp);
    if(s->connection)(void)netconn_delete(s->connection);
    uint32_t generation=s->generation;
    memset(s,0,sizeof(*s));
    s->generation=generation; /* invalidated old ID, never revive it */
    bridge.active--;
}
static ip_addr_t ipv4(struct nas_lwip_bridge_address address) {
    ip_addr_t ip;
    IP4_ADDR(ip_2_ip4(&ip), (address.ipv4>>24)&255U,
             (address.ipv4>>16)&255U,(address.ipv4>>8)&255U,
              address.ipv4&255U);
    return ip;
}

/* Netconn serializes its public API with tcpip_thread, but its PCB is not
 * writable from this bridge worker when LWIP_TCPIP_CORE_LOCKING=0. Queue
 * socket-option mutations in the official TCP/IP callback context and keep
 * the worker's callback state alive until the core signals completion. */
struct ip_option_callback {
    struct netconn *connection;
    bool enabled;
    bool tcp_nodelay;
    uint8_t option;
    struct nas_ksem completed;
    err_t status;
};
static void set_ip_option_on_core(void *arg) {
    struct ip_option_callback *c=(struct ip_option_callback *)arg;
    if(c->tcp_nodelay) {
        if(!c->connection->pcb.tcp)c->status=ERR_CONN;
        else {
            if(c->enabled)tcp_nagle_disable(c->connection->pcb.tcp);
            else tcp_nagle_enable(c->connection->pcb.tcp);
            c->status=ERR_OK;
        }
    } else if(!c->connection->pcb.ip)c->status=ERR_CONN;
    else {
        if(c->enabled)ip_set_option(c->connection->pcb.ip,c->option);
        else ip_reset_option(c->connection->pcb.ip,c->option);
        c->status=ERR_OK;
    }
    (void)nas_ksem_post(&c->completed);
}
static err_t set_ip_option(struct netconn *connection,bool enabled,
                           uint8_t option) {
    struct ip_option_callback c={.connection=connection,.enabled=enabled,
                                 .option=option,.status=ERR_OK};
    nas_ksem_init(&c.completed,0);
    err_t err=tcpip_callback(set_ip_option_on_core,&c);
    if(err!=ERR_OK)return err;
    /* This wait parks a native kernel thread, never a Task/syscall stack. */
    if(!nas_ksem_wait(&c.completed))return ERR_VAL;
    return c.status;
}
static err_t set_tcp_nodelay(struct netconn *connection,bool enabled) {
    struct ip_option_callback c={.connection=connection,.enabled=enabled,
                                 .tcp_nodelay=true,.status=ERR_OK};
    nas_ksem_init(&c.completed,0);
    err_t err=tcpip_callback(set_ip_option_on_core,&c);
    if(err!=ERR_OK)return err;
    if(!nas_ksem_wait(&c.completed))return ERR_VAL;
    return c.status;
}

static void execute(unsigned slot) {
    struct bridge_slot *s=&bridge.slots[slot];
    struct nas_lwip_bridge_result *out=&s->result;
    const struct nas_lwip_bridge_request *r=&s->request;
    memset(out,0,sizeof(*out));
    if((int)r->op==BRIDGE_OPEN) {
        s->connection=netconn_new_with_callback(s->type==NAS_LWIP_BRIDGE_TCP ?
                                                NETCONN_TCP:NETCONN_UDP,
                                                netconn_event);
        if(!s->connection){out->status=-ENOMEM;return;}
        /* The command worker, never a syscall, waits for lwIP core APIs;
         * data and connection admission never wait on remote peers. */
        netconn_set_nonblocking(s->connection,1);
        return;
    }
    if(!s->connection){out->status=-ENOTCONN;return;}
    err_t err=ERR_OK;
    ip_addr_t address=ipv4(r->address);
    switch(r->op) {
    case NAS_LWIP_BRIDGE_BIND:
        err=netconn_bind(s->connection,&address,r->address.port);
        break;
    case NAS_LWIP_BRIDGE_CONNECT:
        err=netconn_connect(s->connection,&address,r->address.port);
        break;
    case NAS_LWIP_BRIDGE_LISTEN:
        if(s->type!=NAS_LWIP_BRIDGE_TCP){err=ERR_ARG;break;}
        s->accept_pending=0;
        err=netconn_listen_with_backlog(s->connection,
                                        (u8_t)(r->backlog>255U?255U:r->backlog));
        if(err==ERR_OK)s->listening=true;
        break;
    case NAS_LWIP_BRIDGE_ACCEPT: {
        if(s->type!=NAS_LWIP_BRIDGE_TCP){err=ERR_ARG;break;}
        if(s->accept_cancelled){out->status=-ECANCELED;return;}
        /* accept_slot is reserved by the future OpenFile caller; the
         * accepted connection is never allocated in an unrelated FD map. */
        if(!accept_lease_valid(r->accept_slot,r->accept_lease_token) ||
           bridge.slots[r->accept_slot].owned ||
           bridge.active>=NAS_LWIP_BRIDGE_ACTIVE_LIMIT) {
            out->status=-ENFILE;return;
        }
        struct netconn *accepted=NULL;
        err=netconn_accept(s->connection,&accepted);
        if(err!=ERR_OK)break;
        if(!accepted){out->status=-EIO;return;}
        /* Linux accept() may request sockaddr_in. Query the ACTUAL peer on
         * this native worker stack; never invent 127.0.0.1 or borrow the
         * listener's local address for an accepted connection. */
        ip_addr_t peer_ip;
        u16_t peer_port=0;
        err=netconn_getaddr(accepted,&peer_ip,&peer_port,0);
        if(err!=ERR_OK || !IP_IS_V4(&peer_ip)) {
            (void)netconn_delete(accepted);
            out->status=err!=ERR_OK ? -err_to_errno(err):-EAFNOSUPPORT;
            return;
        }
        out->peer.ipv4=lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&peer_ip)));
        out->peer.port=peer_port;
        /* A signal/close can cancel a Task's lease while Netconn is in a
         * native wait. Recheck after returning from upstream before ever
         * publishing a child into the former OpenFile slot. */
        if(s->accept_cancelled ||
           !accept_lease_valid(r->accept_slot,r->accept_lease_token)) {
            (void)netconn_delete(accepted);
            out->status=-ECANCELED;
            return;
        }
        struct nas_lwip_bridge_id id=reserve(r->accept_slot,NAS_LWIP_BRIDGE_TCP);
        if(!id.generation) {
            (void)netconn_delete(accepted);
            out->status=-ENFILE;return;
        }
        bridge.slots[id.slot].connection=accepted;
        netconn_set_nonblocking(accepted,1);
        out->accepted=id;
        break;
    }
    case NAS_LWIP_BRIDGE_SEND:
        if(s->type==NAS_LWIP_BRIDGE_TCP) {
            size_t sent=0;
            err=netconn_write_partly(s->connection,s->copied_send,r->length,
                                     NETCONN_COPY|NETCONN_DONTBLOCK,&sent);
            if(sent>NAS_LWIP_BRIDGE_DATA){out->status=-EIO;return;}
            out->length=(uint16_t)sent;
        } else {
            struct netbuf *packet=netbuf_new();
            if(!packet){out->status=-ENOMEM;return;}
            void *data=netbuf_alloc(packet,r->length);
            if(!data && r->length){netbuf_delete(packet);out->status=-ENOMEM;return;}
            if(r->length)memcpy(data,s->copied_send,r->length);
            err=r->address.port ?
                netconn_sendto(s->connection,packet,&address,r->address.port) :
                netconn_send(s->connection,packet);
            if(err==ERR_OK)out->length=r->length;
            netbuf_delete(packet);
        }
        break;
    case NAS_LWIP_BRIDGE_RECV: {
        /* A zero-byte read cannot consume a datagram or TCP segment. */
        if(!r->length)break;
        struct netbuf *packet=s->held_tcp;
        if(!packet) {
            err=netconn_recv(s->connection,&packet);
            if(err==ERR_CLSD) {out->status=0;return;} /* TCP EOF */
            if(err!=ERR_OK)break;
            if(!packet){out->status=-EIO;return;}
        }
        uint16_t total=netbuf_len(packet);
        unsigned remain=(unsigned)total-s->held_offset;
        unsigned n=remain>r->length?r->length:remain;
        if(n && netbuf_copy_partial(packet,out->bytes,(u16_t)n,
                                    s->held_offset)!=(u16_t)n) {
            netbuf_delete(packet);s->held_tcp=NULL;s->held_offset=0;
            out->status=-EIO;return;
        }
        out->length=(uint16_t)n;
        if(s->type==NAS_LWIP_BRIDGE_UDP) {
            const ip_addr_t *from=netbuf_fromaddr(packet);
            if(from)out->peer.ipv4=lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(from)));
            out->peer.port=netbuf_fromport(packet);
            out->truncated=remain>n;
            netbuf_delete(packet);
        } else if(n<remain) {
            s->held_tcp=packet;s->held_offset+=(uint16_t)n;
        } else {
            netbuf_delete(packet);s->held_tcp=NULL;s->held_offset=0;
        }
        break;
    }
    case NAS_LWIP_BRIDGE_GETNAME: {
        ip_addr_t endpoint;
        u16_t port=0;
        err=netconn_getaddr(s->connection,&endpoint,&port,
                            r->name_peer ? 0U:1U);
        if(err!=ERR_OK)break;
        if(!IP_IS_V4(&endpoint)){out->status=-EAFNOSUPPORT;return;}
        out->peer.ipv4=lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&endpoint)));
        out->peer.port=port;
        break;
    }
    case NAS_LWIP_BRIDGE_SHUTDOWN:
        if(s->type!=NAS_LWIP_BRIDGE_TCP){out->status=-EOPNOTSUPP;return;}
        err=netconn_shutdown(s->connection,r->shutdown_how!=1U,
                            r->shutdown_how!=0U);
        break;
    case NAS_LWIP_BRIDGE_SETREUSEADDR:
#if SO_REUSE
        err=set_ip_option(s->connection,r->reuseaddr,SOF_REUSEADDR);
#else
        out->status=-ENOPROTOOPT;
#endif
        break;
    case NAS_LWIP_BRIDGE_SETBROADCAST:
#if IP_SOF_BROADCAST
        if(s->type!=NAS_LWIP_BRIDGE_UDP){out->status=-EOPNOTSUPP;return;}
        err=set_ip_option(s->connection,r->broadcast,SOF_BROADCAST);
#else
        out->status=-ENOPROTOOPT;
#endif
        break;
    case NAS_LWIP_BRIDGE_SETNODELAY:
        if(s->type!=NAS_LWIP_BRIDGE_TCP){out->status=-EOPNOTSUPP;return;}
        err=set_tcp_nodelay(s->connection,r->nodelay);
        break;
    default:out->status=-EINVAL;return;
    }
    if(err!=ERR_OK)out->status=-err_to_errno(err);
}
static int next_closing(void) {
    for(unsigned i=0;i<NAS_LWIP_BRIDGE_SLOTS;i++)
        if(bridge.slots[i].owned && bridge.slots[i].closing &&
           !bridge.slots[i].queued)return (int)i;
    return -1;
}
static void worker(void *unused) {
    (void)unused;
    unsigned worked=0;
    for(;;) {
        if(!nas_ksem_wait(&bridge.wake))return;
        /* One semaphore token represents ONE enqueue/close notification.
         * Draining the entire queue after one wait would strand the other
         * posted tokens and spin a later worker slice without any work. */
        if(bridge.count) {
            unsigned slot=bridge.queue[bridge.first];
            bridge.first=(bridge.first+1U)%NAS_LWIP_BRIDGE_QUEUE;
            bridge.count--;
            struct bridge_slot *s=&bridge.slots[slot];
            s->queued=false;
            if(s->closing)free_native(slot);
            else if(s->owned) {
                s->executing=true;
                execute(slot);
                s->executing=false;
                if(s->accept_cancelled &&
                   s->request.op==NAS_LWIP_BRIDGE_ACCEPT){
                    if(s->result.accepted.generation) {
                        struct bridge_slot *child=lookup(s->result.accepted);
                        if(child){child->closing=true;(void)nas_ksem_post(&bridge.wake);}
                    }
                    memset(&s->result,0,sizeof(s->result));
                    s->result.status=-ECANCELED;
                }
                s->finished=true;
            }
        } else {
            int slot=next_closing();
            if(slot>=0)free_native((unsigned)slot);
            /* A final close of a still-queued command may leave one extra
             * wake token; this single no-op consumes it exactly once. */
        }
        if(++worked==4) {
            /* One native C-stack slice cannot starve user programs. */
            worked=0;
            (void)nas_kthread_yield();
        }
    }
}
bool nas_lwip_bridge_start(void) {
    if(bridge.started)return false;
    nas_ksem_init(&bridge.wake,0);
    if(!nas_kthread_spawn(worker,NULL,&bridge.worker))return false;
    bridge.started=true;
    return true;
}
int nas_lwip_bridge_open(unsigned file_slot,enum nas_lwip_bridge_type type,
                         struct nas_lwip_bridge_id *out) {
    if(!bridge.started || !out)return -EINVAL;
    if(type!=NAS_LWIP_BRIDGE_TCP && type!=NAS_LWIP_BRIDGE_UDP)return -EPROTOTYPE;
    if(bridge.count==NAS_LWIP_BRIDGE_QUEUE)return -EAGAIN;
    struct nas_lwip_bridge_id id=reserve(file_slot,type);
    if(!id.generation)return -ENFILE;
    struct bridge_slot *s=&bridge.slots[id.slot];
    s->request.op=(enum nas_lwip_bridge_op)BRIDGE_OPEN;
    s->queued=true;
    if(!enqueue(id.slot)) {s->queued=false;s->closing=true;return -EAGAIN;}
    *out=id;
    return 0;
}
int nas_lwip_bridge_submit(struct nas_lwip_bridge_id id,
                           const struct nas_lwip_bridge_request *request) {
    struct bridge_slot *s=lookup(id);
    if(!s)return -EBADF;
    if(!request)return -EINVAL;
    if(s->queued || s->executing || s->finished)return -EBUSY;
    if(bridge.count==NAS_LWIP_BRIDGE_QUEUE)return -EAGAIN;
    if(request->op<NAS_LWIP_BRIDGE_BIND ||
       request->op>NAS_LWIP_BRIDGE_SETNODELAY ||
       request->length>NAS_LWIP_BRIDGE_DATA ||
       (request->op==NAS_LWIP_BRIDGE_SHUTDOWN && request->shutdown_how>2U) ||
       (request->op==NAS_LWIP_BRIDGE_SEND && request->length && !request->bytes))
        return -EINVAL;
    if(request->op==NAS_LWIP_BRIDGE_ACCEPT &&
       !accept_lease_valid(request->accept_slot,request->accept_lease_token))
        return -EBADF;
    s->request=*request;
    s->accept_cancelled=false;
    if(request->op==NAS_LWIP_BRIDGE_SEND && request->length)
        memcpy(s->copied_send,request->bytes,request->length);
    s->request.bytes=NULL; /* caller buffer NEVER escapes syscall lifetime */
    s->queued=true;
    if(!enqueue(id.slot)) {s->queued=false;return -EAGAIN;}
    return 0;
}
int nas_lwip_bridge_take(struct nas_lwip_bridge_id id,
                         struct nas_lwip_bridge_result *out) {
    struct bridge_slot *s=lookup(id);
    if(!s)return -EBADF;
    if(!out)return -EINVAL;
    if(!s->finished)return 0;
    *out=s->result;
    s->finished=false;
    return 1;
}
int nas_lwip_bridge_release(struct nas_lwip_bridge_id id) {
    struct bridge_slot *s=lookup(id);
    if(!s)return -EBADF;
    s->closing=true;
    /* A queued command keeps its slot owned until the worker observes it;
     * final release cannot reuse either the Netconn or the generation. */
    (void)nas_ksem_post(&bridge.wake);
    return 0;
}
bool nas_lwip_bridge_alive(struct nas_lwip_bridge_id id){return lookup(id)!=NULL;}
bool nas_lwip_bridge_listener_readable(struct nas_lwip_bridge_id id) {
    struct bridge_slot *s=lookup(id);
    return s && s->listening && s->accept_pending!=0;
}

int nas_lwip_bridge_cancel_accept(struct nas_lwip_bridge_id id,
                                  const struct nas_fd_slot_lease *lease) {
    struct bridge_slot *s=lookup(id);
    if(!s)return -EBADF;
    if(!lease || !lease->token ||
       s->request.op!=NAS_LWIP_BRIDGE_ACCEPT ||
       s->request.accept_slot!=lease->slot ||
       s->request.accept_lease_token!=lease->token)return -EINVAL;
    if(!s->queued && !s->executing && !s->finished)
        return -EALREADY; /* a caller already consumed the result */
    s->accept_cancelled=true;
    if(s->finished) {
        /* If completion raced with interruption, the native worker's
         * unclaimed child still belongs to the listener, not the Task. */
        struct bridge_slot *child=lookup(s->result.accepted);
        if(child) {
            child->closing=true;
            (void)nas_ksem_post(&bridge.wake);
        }
        memset(&s->result,0,sizeof(s->result));
        s->result.status=-ECANCELED;
    }
    return 0;
}
