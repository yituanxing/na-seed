/* Original lwIP Netconn owned by a NaSeed native kthread. No socket parser,
 * second TCP/IP stack, user address, FD namespace or blocking user syscall. */
#include "netconn_bridge.h"
#include "../../kernel/fd.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/task/k_sync.h"
#include "../../kernel/task/kthread.h"
#include "../../kernel/arch/arch.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include <stdint.h>
#include <stdbool.h>

_Static_assert(NAS_LWIP_BRIDGE_SLOTS==NAS_OPEN_FILE_LIMIT,
               "bridge identity must be one global open-description slot");
_Static_assert(NAS_LWIP_BRIDGE_ACTIVE_LIMIT==MEMP_NUM_NETCONN,
               "bridge admission must match the real upstream Netconn pool");

/* Final close cannot wait on the Task stack. The native worker gives a
 * copied, already-admitted TCP tail a finite opportunity to enter lwIP's
 * TCP send buffer before FIN. TCP retransmission thereafter belongs to
 * upstream lwIP, not this bridge. */
#define CLOSE_TX_RETRY_NS UINT64_C(20000000)
#define CLOSE_TX_DEADLINE_NS UINT64_C(1000000000)
#define CLOSE_TX_MAX_ATTEMPTS 64U
#define CLOSE_UDP_RETRY_NS UINT64_C(1000000)
#define CLOSE_UDP_CAPACITY (NAS_LWIP_UDP_PENDING+1U)

enum bridge_internal_op { BRIDGE_OPEN=100 };
struct bridge_slot {
    uint32_t generation;
    bool owned,closing,queued,executing,finished,accept_cancelled,listening;
    bool close_tx_active,close_tx_wait,close_tx_result_pending;
    unsigned accept_pending;
    unsigned close_tx_attempts;
    uint16_t close_tx_position,close_tx_length;
    uint64_t close_tx_retry_at,close_tx_deadline;
    uint8_t close_udp_count,close_udp_position;
    struct nas_lwip_bridge_datagram close_udp[CLOSE_UDP_CAPACITY];
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
    void (*completion_hook)(struct nas_lwip_bridge_id);
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
        if(!s->owned || s->connection!=connection)continue;
        if(s->closing) {
            /* A full send queue can reopen when the peer ACKs. Wake only
             * an actual waiting close, never repeatedly post stale tokens
             * or reenter Netconn from its core callback. */
            if(event==NETCONN_EVT_SENDPLUS && s->close_tx_wait) {
                s->close_tx_wait=false;
                (void)nas_ksem_post(&bridge.wake);
            }
            return;
        }
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
#ifdef NASEED_SYSCALL_TRACE
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
/* Only print on genuine native bridge admission failure. Socket count must
 * never be increased to conceal a failed final-close/reclaim path. */
static void trace_bridge_exhaustion(unsigned requested) {
    nas_print("[LWIP BRIDGE CAP] requested=");nas_dec(requested);
    nas_print(" active=");nas_dec(bridge.active);
    nas_print(" capacity=");nas_dec(NAS_LWIP_BRIDGE_ACTIVE_LIMIT);
    nas_print("\n");
    for(unsigned i=0;i<NAS_LWIP_BRIDGE_SLOTS;i++) {
        const struct bridge_slot *s=&bridge.slots[i];
        if(!s->owned)continue;
        nas_print("[LWIP BRIDGE CAP SLOT] id=");nas_dec(i);
        nas_print(" ref=");nas_dec(nas_open_files[i].refcount);
        nas_print(" closing=");nas_dec(s->closing);
        nas_print(" queued=");nas_dec(s->queued);
        nas_print(" executing=");nas_dec(s->executing);
        nas_print(" finished=");nas_dec(s->finished);
        nas_print(" tx_pending=");nas_dec(s->close_tx_active);
        nas_print("\n");
    }
}
#endif
static struct nas_lwip_bridge_id reserve(unsigned slot,
                                         enum nas_lwip_bridge_type type) {
    struct nas_lwip_bridge_id none={0};
    if(slot>=NAS_LWIP_BRIDGE_SLOTS || bridge.active>=NAS_LWIP_BRIDGE_ACTIVE_LIMIT ||
       bridge.slots[slot].owned) {
#ifdef NASEED_SYSCALL_TRACE
        trace_bridge_exhaustion(slot);
#endif
        return none;
    }
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

static uint64_t add_deadline(uint64_t now,uint64_t duration) {
    return now<=UINT64_MAX-duration ? now+duration:UINT64_MAX;
}
static bool deadline_expired(uint64_t now,uint64_t deadline) {
    return now>=deadline;
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
    bool pktinfo;
    uint8_t option;
    struct nas_ksem completed;
    err_t status;
};
static void set_ip_option_on_core(void *arg) {
    struct ip_option_callback *c=(struct ip_option_callback *)arg;
    if(c->pktinfo) {
        if(!c->connection->pcb.udp)c->status=ERR_CONN;
        else {
            if(c->enabled)netconn_set_flags(c->connection,NETCONN_FLAG_PKTINFO);
            else netconn_clear_flags(c->connection,NETCONN_FLAG_PKTINFO);
            c->status=ERR_OK;
        }
    } else if(c->tcp_nodelay) {
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
static err_t set_udp_pktinfo(struct netconn *connection,bool enabled) {
    struct ip_option_callback c={.connection=connection,.enabled=enabled,
                                 .pktinfo=true,.status=ERR_OK};
    nas_ksem_init(&c.completed,0);
    err_t err=tcpip_callback(set_ip_option_on_core,&c);
    if(err!=ERR_OK)return err;
    if(!nas_ksem_wait(&c.completed))return ERR_VAL;
    return c.status;
}

/* netconn_sendto has no per-datagram source/netif arguments. For Linux
 * sendmsg(IP_PKTINFO), run the ORIGINAL upstream raw UDP send helper on
 * tcpip_thread with the same Netconn-owned PCB and pbuf. The native bridge
 * worker retains the pbuf and callback state until completion. */
struct udp_send_pktinfo_callback {
    struct netconn *connection;
    struct pbuf *packet;
    struct nas_lwip_bridge_address destination;
    struct nas_lwip_bridge_pktinfo info;
    struct nas_ksem completed;
    err_t status;
};
static void send_udp_pktinfo_on_core(void *arg) {
    struct udp_send_pktinfo_callback *c=arg;
    struct udp_pcb *pcb=c->connection->pcb.udp;
    if(!pcb){c->status=ERR_CONN;goto complete;}
    const ip_addr_t dest=ipv4(c->destination);
    struct netif *netif=NULL;
    if(c->info.ifindex) {
        if(c->info.ifindex>UINT8_MAX){c->status=ERR_IF;goto complete;}
        netif=netif_get_by_index((u8_t)c->info.ifindex);
    } else netif=ip_route(&pcb->local_ip,&dest);
    if(!netif){c->status=ERR_RTE;goto complete;}
    if(pcb->netif_idx && pcb->netif_idx!=netif_get_index(netif)) {
        /* SO_BINDTODEVICE is a real PCB constraint, not a hint that a
         * per-message CMSG may silently defeat. */
        c->status=ERR_IF;goto complete;
    }
    if(c->info.spec_dst) {
        const uint32_t source=c->info.spec_dst;
        /* The source must be assigned to the selected real netif. With
         * LWIP_HAVE_LOOPIF the 127/8 route has its own 127.0.0.1 netif;
         * an arbitrary physical NIC cannot spoof loopback as source. */
        const uint32_t actual=lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(netif)));
        if(source!=actual) {
            c->status=ERR_IF;goto complete;
        }
        const ip_addr_t src=ipv4((struct nas_lwip_bridge_address){.ipv4=source});
        c->status=udp_sendto_if_src(pcb,c->packet,&dest,
                                    c->destination.port,netif,&src);
    } else c->status=udp_sendto_if(pcb,c->packet,&dest,
                                   c->destination.port,netif);
complete:
    (void)nas_ksem_post(&c->completed);
}
static err_t send_udp_pktinfo(struct netconn *connection,struct pbuf *p,
                               struct nas_lwip_bridge_address destination,
                               struct nas_lwip_bridge_pktinfo info) {
    struct udp_send_pktinfo_callback c={.connection=connection,.packet=p,
        .destination=destination,.info=info,.status=ERR_OK};
    nas_ksem_init(&c.completed,0);
    err_t err=tcpip_callback(send_udp_pktinfo_on_core,&c);
    if(err!=ERR_OK)return err;
    if(!nas_ksem_wait(&c.completed))return ERR_VAL;
    return c.status;
}

/* Shared by ordinary SEND and final-close UDP drain. Always submit a whole
 * datagram via upstream lwIP; packet boundaries, destination and IP_PKTINFO
 * never depend on which owner currently holds the copied bytes. */
static int send_udp_datagram(struct netconn *connection,
                             const uint8_t *bytes,uint16_t length,
                             struct nas_lwip_bridge_address destination,
                             bool send_pktinfo,
                             struct nas_lwip_bridge_pktinfo packet_info) {
    struct netbuf *packet=netbuf_new();
    if(!packet)return -ENOMEM;
    void *data=netbuf_alloc(packet,length);
    if(!data && length){netbuf_delete(packet);return -ENOMEM;}
    if(length)memcpy(data,bytes,length);
    err_t err=ERR_OK;
    if(send_pktinfo) {
        if(!destination.port) { /* connected UDP sendmsg(NULL name) */
            ip_addr_t peer;
            u16_t peer_port=0;
            err=netconn_getaddr(connection,&peer,&peer_port,0);
            if(err==ERR_OK && (!peer_port || !IP_IS_V4(&peer)))
                err=ERR_CONN;
            if(err==ERR_OK) {
                destination.ipv4=lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&peer)));
                destination.port=peer_port;
            }
        }
        if(err==ERR_OK)err=send_udp_pktinfo(connection,packet->p,
                                             destination,packet_info);
    } else if(destination.port) {
        const ip_addr_t addr=ipv4(destination);
        err=netconn_sendto(connection,packet,&addr,destination.port);
    } else err=netconn_send(connection,packet);
    netbuf_delete(packet);
    return err==ERR_OK ? 0:-err_to_errno(err);
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
        if(s->type==NAS_LWIP_BRIDGE_UDP)
            netconn_clear_flags(s->connection,NETCONN_FLAG_PKTINFO);
        return;
    }
    if(!s->connection){out->status=-ENOTCONN;return;}
    err_t err=ERR_OK;
    ip_addr_t address=ipv4(r->address);
    switch(r->op) {
    case NAS_LWIP_BRIDGE_BIND:
        err=netconn_bind(s->connection,&address,r->address.port);
        break;
    case NAS_LWIP_BRIDGE_BIND_IF:
        /* Official lwIP updates the actual UDP/TCP PCB's bound netif.
         * Unknown indices are rejected by upstream, never reported bound. */
        err=netconn_bind_if(s->connection,r->if_index);
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
            out->status=send_udp_datagram(s->connection,s->copied_send,
                r->length,r->address,r->send_pktinfo,r->packet_info);
            if(out->status==0)out->length=r->length;
            return;
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
#if LWIP_NETBUF_RECVINFO
            if(packet->flags&NETBUF_FLAG_DESTADDR &&
               IP_IS_V4(netbuf_destaddr(packet))) {
                out->packet_info.destination=lwip_ntohl(ip4_addr_get_u32(
                    ip_2_ip4(netbuf_destaddr(packet))));
                out->packet_info.ifindex=packet->p->if_idx;
                /* Linux ipi_spec_dst is a usable local source address,
                 * not the broadcast/multicast IPv4 destination. */
                out->packet_info.spec_dst=out->packet_info.destination;
                struct netif *received_if=netif_get_by_index(
                    (u8_t)out->packet_info.ifindex);
                if(received_if &&
                   (ip4_addr_ismulticast(ip_2_ip4(netbuf_destaddr(packet))) ||
                    ip4_addr_isbroadcast(ip_2_ip4(netbuf_destaddr(packet)),
                                         received_if)))
                    out->packet_info.spec_dst=lwip_ntohl(ip4_addr_get_u32(
                        netif_ip4_addr(received_if)));
            }
#endif
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
    case NAS_LWIP_BRIDGE_SETKEEPALIVE:
        if(s->type!=NAS_LWIP_BRIDGE_TCP){out->status=-EOPNOTSUPP;return;}
        /* The upstream slow TCP timer checks this PCB bit and sends real
         * keepalive probes (with lwIP's configured default probe timings).
         * Only tcpip_thread may mutate it; a local cache alone is not an
         * implementation of SO_KEEPALIVE. */
        err=set_ip_option(s->connection,r->keepalive,SOF_KEEPALIVE);
        break;
    case NAS_LWIP_BRIDGE_SETPKTINFO:
        if(s->type!=NAS_LWIP_BRIDGE_UDP){out->status=-EOPNOTSUPP;return;}
        err=set_udp_pktinfo(s->connection,r->pktinfo);
        break;
    default:out->status=-EINVAL;return;
    }
    if(err!=ERR_OK)out->status=-err_to_errno(err);
}
/* This is a single-buffer, finite, asynchronous final-close drain. All
 * Netconn calls still happen in the one native bridge worker. `copied_send`
 * is retained under the old bridge generation until either upstream lwIP
 * accepts it, a real error arrives, or the close deadline expires. */
static void finish_closing(unsigned slot) {
    struct bridge_slot *s=&bridge.slots[slot];
    if(s->type==NAS_LWIP_BRIDGE_UDP) {
        if(!s->connection ||
           s->close_udp_position==s->close_udp_count ||
           deadline_expired(nas_arch_monotonic_ns(),s->close_tx_deadline) ||
           s->close_tx_attempts>=CLOSE_TX_MAX_ATTEMPTS) {
            free_native(slot);
            return;
        }
        const struct nas_lwip_bridge_datagram *packet=
            &s->close_udp[s->close_udp_position];
        int rc=send_udp_datagram(s->connection,packet->bytes,packet->length,
                                  packet->destination,packet->send_pktinfo,
                                  packet->packet_info);
        s->close_tx_attempts++;
        if(!rc) {
            s->close_udp_position++;
            if(s->close_udp_position==s->close_udp_count) {
                free_native(slot);
                return;
            }
        } else if(rc!=-EAGAIN && rc!=-ENOMEM && rc!=-ENOBUFS) {
            free_native(slot);
            return;
        }
        /* Give each queued packet a native worker slice, rather than
         * spinning inside final close while other sockets need service. */
        s->close_tx_wait=true;
        s->close_tx_retry_at=add_deadline(nas_arch_monotonic_ns(),
                                           CLOSE_UDP_RETRY_NS);
        return;
    }
    if(!s->close_tx_active || !s->connection ||
       s->type!=NAS_LWIP_BRIDGE_TCP) {
        free_native(slot);
        return;
    }
    if(s->close_tx_result_pending) {
        /* A normal SEND may have finished after the Task called close().
         * Its accepted prefix is already inside TCP; never send it again. */
        if(!s->finished)return;
        if(s->result.length>s->close_tx_length) {
            free_native(slot);
            return;
        }
        s->close_tx_position=s->result.length;
        s->close_tx_result_pending=false;
        if(s->result.status && s->result.status!=-EAGAIN &&
           s->result.status!=-EINPROGRESS &&
           s->result.status!=-ENOMEM) {
            free_native(slot);
            return;
        }
    }
    const uint64_t now=nas_arch_monotonic_ns();
    if(s->close_tx_position==s->close_tx_length ||
       deadline_expired(now,s->close_tx_deadline) ||
       s->close_tx_attempts>=CLOSE_TX_MAX_ATTEMPTS) {
        free_native(slot);
        return;
    }
    ++s->close_tx_attempts;
    const uint16_t remaining=s->close_tx_length-s->close_tx_position;
    size_t sent=0;
    err_t err=netconn_write_partly(s->connection,
                                  s->copied_send+s->close_tx_position,
                                  remaining,NETCONN_COPY|NETCONN_DONTBLOCK,
                                  &sent);
    if(sent>remaining) {
        free_native(slot);
        return;
    }
    s->close_tx_position+=(uint16_t)sent;
    if(s->close_tx_position==s->close_tx_length ||
       (err!=ERR_OK && err!=ERR_WOULDBLOCK && err!=ERR_MEM)) {
        free_native(slot);
        return;
    }
    /* TCP may need a genuine ACK/window update before accepting the rest.
     * Its SENDPLUS event expedites retry; the monotonic timer also retries
     * every 20ms and enforces the absolute 1s / 64-attempt upper bound. */
    s->close_tx_wait=true;
    s->close_tx_retry_at=add_deadline(now,CLOSE_TX_RETRY_NS);
}
static int next_closing(void) {
    uint64_t now=nas_arch_monotonic_ns();
    for(unsigned i=0;i<NAS_LWIP_BRIDGE_SLOTS;i++) {
        const struct bridge_slot *s=&bridge.slots[i];
        if(!s->owned || !s->closing || s->queued)continue;
        if(!s->close_tx_wait ||
           deadline_expired(now,s->close_tx_retry_at) ||
           deadline_expired(now,s->close_tx_deadline))return (int)i;
    }
    return -1;
}
static uint64_t next_closing_retry(void) {
    uint64_t earliest=0;
    for(unsigned i=0;i<NAS_LWIP_BRIDGE_SLOTS;i++) {
        const struct bridge_slot *s=&bridge.slots[i];
        if(!s->owned || !s->closing || !s->close_tx_wait)continue;
        uint64_t deadline=s->close_tx_retry_at<s->close_tx_deadline ?
                          s->close_tx_retry_at:s->close_tx_deadline;
        if(!earliest || deadline<earliest)earliest=deadline;
    }
    return earliest;
}
static void worker(void *unused) {
    (void)unused;
    unsigned worked=0;
    for(;;) {
        enum nas_kwait_result wait=nas_ksem_wait_until(
            &bridge.wake,next_closing_retry());
        if(wait==NAS_KWAIT_INVALID)return;
        /* Each posted token represents one enqueue/close notification;
         * timeout wakes additionally retry an old close's copied suffix.
         * Draining the entire queue after one wait would strand its other
         * posted tokens and spin a later worker slice without any work. */
        if(bridge.count) {
            unsigned slot=bridge.queue[bridge.first];
            bridge.first=(bridge.first+1U)%NAS_LWIP_BRIDGE_QUEUE;
            bridge.count--;
            struct bridge_slot *s=&bridge.slots[slot];
            s->queued=false;
            if(s->closing) {
                /* If close raced a queued SEND, first consume the exact
                 * copied command. The finish step accounts for partial
                 * admission before trying its still-owned suffix. */
                if(s->close_tx_result_pending ||
                   (s->type==NAS_LWIP_BRIDGE_UDP &&
                    (s->close_udp_count ||
                     (s->connection &&
                      s->request.op==NAS_LWIP_BRIDGE_SEND)))) {
                    execute(slot);
                    s->finished=true;
                }
                finish_closing(slot);
            }
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
                /* A File may have accepted multiple UDP datagrams without
                 * another userspace operation. Notify its owner after the
                 * worker publishes completion, so the owner can advance
                 * its FIFO; the bridge itself never reads OpenFile state.
                 * The callback is immediate and cannot run Netconn. */
                if(s->type==NAS_LWIP_BRIDGE_UDP &&
                   bridge.completion_hook)
                    bridge.completion_hook((struct nas_lwip_bridge_id){
                        (uint16_t)slot,s->generation
                    });
            }
        } else {
            int slot=next_closing();
            if(slot>=0)finish_closing((unsigned)slot);
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
void nas_lwip_bridge_set_completion_hook(
                         void (*hook)(struct nas_lwip_bridge_id)) {
    bridge.completion_hook=hook;
}
bool nas_lwip_bridge_slot_available(unsigned slot) {
    return slot<NAS_LWIP_BRIDGE_SLOTS && !bridge.slots[slot].owned;
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
       request->op>NAS_LWIP_BRIDGE_SETPKTINFO ||
       request->length>NAS_LWIP_BRIDGE_DATA ||
       (request->op==NAS_LWIP_BRIDGE_BIND_IF && !request->if_index) ||
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
int nas_lwip_bridge_release_with_tail(struct nas_lwip_bridge_id id,
                                      const uint8_t *tail,uint16_t length) {
    struct bridge_slot *s=lookup(id);
    if(!s)return -EBADF;
    if(length>NAS_LWIP_BRIDGE_DATA || (length && !tail))return -EINVAL;
    if(length && s->type!=NAS_LWIP_BRIDGE_TCP)return -EOPNOTSUPP;
    if(length && (s->queued || s->executing || s->finished))
        return -EBUSY; /* caller must not overwrite another worker request */
    if(length) {
        /* SocketPort completed one native partial SEND, and this is its
         * not-yet-submitted copied remainder. Preserve that buffer under
         * the old generation before SocketPort destroys its File state. */
        memcpy(s->copied_send,tail,length);
        s->request=(struct nas_lwip_bridge_request){
            .op=NAS_LWIP_BRIDGE_SEND,.length=length
        };
    }
    if(s->type==NAS_LWIP_BRIDGE_TCP &&
       (length || (s->request.op==NAS_LWIP_BRIDGE_SEND &&
                   (s->queued || s->executing || s->finished)))) {
        s->close_tx_active=true;
        s->close_tx_length=length ? length:s->request.length;
        s->close_tx_position=0;
        s->close_tx_result_pending=!length;
        s->close_tx_deadline=add_deadline(nas_arch_monotonic_ns(),
                                           CLOSE_TX_DEADLINE_NS);
    }
    s->closing=true;
    /* A queued command keeps its slot owned until the worker observes it;
     * the native close drain also pins the old slot/generation, preventing
     * an unrelated new FD from claiming its worker-owned Netconn. */
    (void)nas_ksem_post(&bridge.wake);
    return 0;
}
int nas_lwip_bridge_release_with_udp(struct nas_lwip_bridge_id id,
                             const struct nas_lwip_bridge_datagram *active,
                             const struct nas_lwip_bridge_datagram *ring,
                             unsigned first,unsigned count) {
    struct bridge_slot *s=lookup(id);
    if(!s)return -EBADF;
    if(s->type!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    if(count>NAS_LWIP_UDP_PENDING || (count && (!ring ||
       first>=NAS_LWIP_UDP_PENDING)))return -EINVAL;
    const unsigned total=count+(active ? 1U:0U);
    if(total>CLOSE_UDP_CAPACITY)return -EINVAL;
    if(active && (!active->length || active->length>NAS_LWIP_BRIDGE_DATA))
        return -EINVAL;
    for(unsigned i=0;i<count;i++) {
        unsigned idx=(first+i)%NAS_LWIP_UDP_PENDING;
        if(!ring[idx].length || ring[idx].length>NAS_LWIP_BRIDGE_DATA)
            return -EINVAL;
    }
    unsigned offset=0;
    if(active)s->close_udp[offset++]=*active;
    for(unsigned i=0;i<count;i++)
        s->close_udp[offset++]=ring[(first+i)%NAS_LWIP_UDP_PENDING];
    s->close_udp_count=(uint8_t)total;
    s->close_udp_position=0;
    s->close_tx_deadline=add_deadline(nas_arch_monotonic_ns(),
                                       CLOSE_TX_DEADLINE_NS);
    s->closing=true;
    /* The old Netconn identity stays reserved until every already accepted
     * datagram has reached the native send API (or a real bounded error).
     * No user buffer or new File generation is accessed after this call. */
    (void)nas_ksem_post(&bridge.wake);
    return 0;
}
int nas_lwip_bridge_release(struct nas_lwip_bridge_id id) {
    return nas_lwip_bridge_release_with_tail(id,NULL,0);
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
