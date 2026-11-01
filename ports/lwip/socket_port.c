/* AF_INET OpenFile provider over the upstream lwIP native Netconn worker.
 * User Tasks never run Netconn, park inside sys_arch, or own socket FDs here.
 *
 * Important bounded contract: write() accepts up to 512 bytes into an owned
 * kernel-side send queue, then returns the accepted byte count. Native SEND
 * completion is asynchronous; any subsequent failure is retained for a later
 * write()/SO_ERROR/POLLERR rather than falsely claiming network delivery.
 * A single worker command per bridge ID serializes control, RX and TX.
 * No netconn readiness callback exists yet: requested RX readiness is probed
 * at most once per millisecond as poll/scheduler queries the description.
 */
#include "socket_port.h"
#include "../../kernel/fd.h"
#include "../../kernel/fs/file_ops.h"
#include "../../kernel/task.h"
#include "../../kernel/arch/arch.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "lwip/opt.h"

#define PORT_POLLIN 0x0001U
#define PORT_POLLOUT 0x0004U
#define PORT_POLLERR 0x0008U
#define PORT_POLLHUP 0x0010U
#define PORT_RETRY_NS UINT64_C(1000000)

enum port_command {
    PORT_IDLE=0, PORT_OPEN, PORT_CONTROL, PORT_CONTROL_DONE,
    PORT_RX, PORT_TX, PORT_CONNECT_PROBE, PORT_DEFERRED_NODELAY, PORT_FAILED
};
struct port_socket {
    bool owned,connected,connecting,listening,queued_control;
    bool connect_result_pending;
    bool accept_owned,accept_cancelling,prefetch_owned;
    bool shut_read,shut_write,peer_known;
    struct nas_fd_slot_lease accept_lease;
    struct nas_fd_slot_lease prefetch_lease;
    bool rx_valid,rx_eof,rx_truncated;
    enum nas_lwip_bridge_type type;
    enum port_command command;
    struct nas_lwip_bridge_id id;
    struct nas_lwip_bridge_result control_result;
    struct nas_lwip_bridge_request control_request;
    struct nas_lwip_bridge_address connect_address;
    struct nas_lwip_bridge_address peer_address;
    struct nas_lwip_bridge_address tx_destination,rx_source;
    bool ip_recverr,tcp_nodelay;
    /* Nonblocking connect() may return to userspace before Netconn finishes.
     * A subsequent TCP_NODELAY is accepted as a same-File pending mutation,
     * applied by the native worker before the first stream data transfer. */
    bool deferred_nodelay,deferred_nodelay_value;
    uint8_t bound_ifindex;
    uint8_t rx[NAS_LWIP_BRIDGE_DATA];
    uint16_t rx_length,rx_position;
    uint8_t tx[NAS_LWIP_BRIDGE_DATA];
    uint16_t tx_length,tx_position;
    int failure,rx_error,tx_error,connect_result;
    uint64_t rx_retry_at,tx_retry_at;
};
static struct port_socket sockets[NAS_OPEN_FILE_LIMIT];

_Static_assert(NAS_OPEN_FILE_LIMIT==NAS_LWIP_BRIDGE_SLOTS,
               "socket provider indexes the same OpenFile slots as bridge");

static struct port_socket *socket_at(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount ||
       !sockets[slot].owned)return NULL;
    return &sockets[slot];
}

static bool elapsed(uint64_t deadline) {
    return !deadline || (int64_t)(nas_arch_monotonic_ns()-deadline)>=0;
}

static uint64_t after_retry(void) {
    uint64_t now=nas_arch_monotonic_ns();
    return now<=UINT64_MAX-PORT_RETRY_NS ? now+PORT_RETRY_NS : UINT64_MAX;
}

/* Called only from kernel callbacks; bridge_take is immediate and never
 * enters upstream Netconn. Its worker has already copied the result. */
static void finish(struct port_socket *s) {
    if(s->command!=PORT_OPEN && s->command!=PORT_CONTROL &&
       s->command!=PORT_RX && s->command!=PORT_TX &&
       s->command!=PORT_CONNECT_PROBE &&
       s->command!=PORT_DEFERRED_NODELAY)return;
    struct nas_lwip_bridge_result result={0};
    int got=nas_lwip_bridge_take(s->id,&result);
    if(got==0)return;
    if(got<0) {
        s->failure=got;
        s->command=PORT_FAILED;
        return;
    }
    switch(s->command) {
    case PORT_DEFERRED_NODELAY:
        if(result.status)s->tx_error=result.status;
        else s->tcp_nodelay=s->deferred_nodelay_value;
        s->command=PORT_IDLE;
        break;
    case PORT_OPEN:
        if(result.status) {
            s->failure=result.status;
            s->command=PORT_FAILED;
        } else s->command=PORT_IDLE;
        break;
    case PORT_CONTROL:
        if(s->control_request.op==NAS_LWIP_BRIDGE_CONNECT) {
            /* An asynchronous TCP handshake must progress even when the
             * caller returned EINPROGRESS and never explicitly takes the
             * first native result. Do not block the sole Netconn worker by
             * retaining PORT_CONTROL_DONE while waiting for user polling. */
            /* UDP connect only sets a local peer association, not a TCP
             * handshake. Linux reports immediate success on enqueue;
             * finish later applies the true Netconn status and SO_ERROR. */
            /* Blocking connect() has a Task waiting for this one-shot
             * result. Nonblocking connect() already returned EINPROGRESS:
             * no Task will consume control_finish(), and retaining the
             * result would permanently reject later getsockname(), bind()
             * or other controls with EBUSY, even after POLLOUT/SO_ERROR.
             * Keep any eventual failure in tx_error for SO_ERROR/POLLERR. */
            s->connect_result_pending=s->type==NAS_LWIP_BRIDGE_TCP &&
                !(nas_open_files[s->id.slot].status_flags&0x800ULL);
            s->command=PORT_IDLE;
            if(result.status==0 || result.status==-EISCONN) {
                s->connected=true;
                s->connecting=false;
                s->connect_result=0;
                if(!s->peer_known) {
                    s->peer_address=s->control_request.address;
                    s->peer_known=true;
                }
            } else if(result.status==-EINPROGRESS ||
                      result.status==-EALREADY) {
                s->connected=false;
                s->connecting=true;
                s->connect_result=-EINPROGRESS;
                s->connect_address=s->control_request.address;
                s->tx_retry_at=after_retry();
            } else {
                s->connected=false;
                s->connecting=false;
                s->connect_result=result.status;
                s->tx_error=result.status;
            }
        } else {
            if(s->control_request.op==NAS_LWIP_BRIDGE_SETNODELAY &&
               result.status==0)
                s->tcp_nodelay=s->control_request.nodelay;
            if(s->control_request.op==NAS_LWIP_BRIDGE_BIND_IF &&
               result.status==0)
                s->bound_ifindex=s->control_request.if_index;
            s->control_result=result;
            s->command=PORT_CONTROL_DONE;
        }
        break;
    case PORT_RX:
        s->command=PORT_IDLE;
        if(result.status==-EAGAIN || result.status==-EINPROGRESS) {
            s->rx_retry_at=after_retry();
        } else if(result.status) {
            s->rx_error=result.status;
        } else if(result.length || s->type==NAS_LWIP_BRIDGE_UDP) {
            s->rx_valid=true;
            s->rx_truncated=result.truncated;
            s->rx_source=result.peer;
            s->rx_length=result.length;
            s->rx_position=0;
            if(result.length)memcpy(s->rx,result.bytes,result.length);
        } else s->rx_eof=true; /* zero-length TCP read = FIN */
        break;
    case PORT_TX:
        s->command=PORT_IDLE;
        if(result.status==-EAGAIN || result.status==-EINPROGRESS) {
            s->tx_retry_at=after_retry();
        } else if(result.status) {
            s->tx_error=result.status;
            s->tx_length=s->tx_position=0;
        } else {
            if(result.length>s->tx_length-s->tx_position) {
                s->tx_error=-EIO;
                s->tx_length=s->tx_position=0;
            } else {
                s->tx_position+=(uint16_t)result.length;
                if(s->tx_position==s->tx_length)
                    s->tx_length=s->tx_position=0;
                else s->tx_retry_at=result.length ? 0 : after_retry();
            }
        }
        break;
    case PORT_CONNECT_PROBE:
        s->command=PORT_IDLE;
        if(result.status==0 || result.status==-EISCONN) {
            s->connecting=false;
            s->connected=true;
            s->connect_result=0;
            if(!s->peer_known) {
                s->peer_address=s->connect_address;
                s->peer_known=true;
            }
        } else if(result.status==-EALREADY || result.status==-EINPROGRESS) {
            s->tx_retry_at=after_retry();
        } else {
            s->connecting=false;
            s->connected=false;
            s->connect_result=result.status;
            s->tx_error=result.status;
        }
        break;
    default:break;
    }
}

static void submit_tx(struct port_socket *s) {
    if(s->command!=PORT_IDLE || s->tx_position>=s->tx_length ||
       !elapsed(s->tx_retry_at))return;
    struct nas_lwip_bridge_request r={
        .op=NAS_LWIP_BRIDGE_SEND,
        .address=s->tx_destination,
        .bytes=s->tx+s->tx_position,
        .length=(uint16_t)(s->tx_length-s->tx_position)
    };
    int rc=nas_lwip_bridge_submit(s->id,&r);
    if(!rc)s->command=PORT_TX;
    else if(rc!=-EAGAIN && rc!=-EBUSY) {
        s->tx_error=rc;
        s->tx_length=s->tx_position=0;
    }
}

/* RX only runs when requested via read or POLLIN. A read of an idle UDP
 * socket does not consume/spin on empty datagrams; the native worker owns
 * one nonblocking RECV and a deadline gates subsequent empty retries. */
static void submit_rx(struct port_socket *s) {
    if(s->command!=PORT_IDLE || s->shut_read || s->rx_valid || s->rx_eof || s->rx_error ||
       s->tx_length || s->listening ||
       (s->type==NAS_LWIP_BRIDGE_TCP && !s->connected) ||
       !elapsed(s->rx_retry_at))return;
    struct nas_lwip_bridge_request r={.op=NAS_LWIP_BRIDGE_RECV,
                                       .length=NAS_LWIP_BRIDGE_DATA};
    int rc=nas_lwip_bridge_submit(s->id,&r);
    if(!rc)s->command=PORT_RX;
    else if(rc!=-EAGAIN && rc!=-EBUSY)s->rx_error=rc;
}

static void submit_probe(struct port_socket *s) {
    if(s->command!=PORT_IDLE || !s->connecting ||
       !elapsed(s->tx_retry_at))return;
    struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_CONNECT,.address=s->connect_address
    };
    if(!nas_lwip_bridge_submit(s->id,&req))s->command=PORT_CONNECT_PROBE;
}

static int check(struct port_socket *s) {
    finish(s);
    if(s->command==PORT_FAILED)return s->failure ? s->failure:-EIO;
    if(s->accept_cancelling && s->command==PORT_CONTROL_DONE &&
       s->control_request.op==NAS_LWIP_BRIDGE_ACCEPT) {
        /* The native worker has observed cancellation and published its
         * terminal result. Drain it even if the interrupted Task will never
         * call accept_complete again; the next accept can reuse this File. */
        if(s->control_result.accepted.generation)
            (void)nas_lwip_bridge_release(s->control_result.accepted);
        s->control_result=(struct nas_lwip_bridge_result){0};
        s->control_request=(struct nas_lwip_bridge_request){0};
        s->command=PORT_IDLE;
        s->accept_cancelling=false;
    }
    /* Apply a deferred option on the actual upstream TCP PCB after the
     * nonblocking CONNECT worker command finishes, before admitting data.
     * The original caller already returned from setsockopt; failures reach
     * SO_ERROR/POLLERR instead of pretending the mutation succeeded. */
    if(s->command==PORT_IDLE && s->deferred_nodelay) {
        struct nas_lwip_bridge_request option={
            .op=NAS_LWIP_BRIDGE_SETNODELAY,
            .nodelay=s->deferred_nodelay_value
        };
        int rc=nas_lwip_bridge_submit(s->id,&option);
        if(!rc) {
            s->deferred_nodelay=false;
            s->command=PORT_DEFERRED_NODELAY;
        } else if(rc!=-EAGAIN && rc!=-EBUSY) {
            s->deferred_nodelay=false;
            s->tx_error=rc;
        }
    }
    /* Shutdown of a TCP stream follows the already-admitted copied SEND
     * queue. A caller may write() then immediately SHUT_WR: userspace has
     * been told its bytes were accepted, so we must never issue FIN first. */
    if(s->command==PORT_IDLE && s->tx_length)submit_tx(s);
    if(s->command==PORT_IDLE && s->queued_control && !s->tx_length) {
        if(s->tx_error) {
            s->control_result=(struct nas_lwip_bridge_result){
                .status=s->tx_error
            };
            s->queued_control=false;
            s->command=PORT_CONTROL_DONE;
            return 0;
        }
        int rc=nas_lwip_bridge_submit(s->id,&s->control_request);
        if(!rc) {
            s->queued_control=false;
            s->command=PORT_CONTROL;
        } else if(rc!=-EAGAIN && rc!=-EBUSY) {
            s->queued_control=false;
            s->failure=rc;
            s->command=PORT_FAILED;
            return rc;
        }
    }
    if(s->command==PORT_IDLE)submit_probe(s);
    return 0;
}

static bool send_shutdown_requested(const struct port_socket *s) {
    return s->shut_write ||
           ((s->queued_control || s->command==PORT_CONTROL ||
             s->command==PORT_CONTROL_DONE) &&
            s->control_request.op==NAS_LWIP_BRIDGE_SHUTDOWN &&
            s->control_request.shutdown_how!=0);
}

static bool accept_finished(const struct port_socket *s) {
    return s->accept_owned && s->command==PORT_CONTROL_DONE &&
           s->control_request.op==NAS_LWIP_BRIDGE_ACCEPT;
}

static int nas_lwip_socket_accept_cancel_internal(
    struct port_socket *s,struct nas_fd_slot_lease lease);

const struct nas_file_ops *nas_lwip_socket_port_ops(void);

int nas_lwip_socket_port_open(unsigned slot,enum nas_lwip_bridge_type type) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    if(sockets[slot].owned)return -EBUSY;
    struct nas_lwip_bridge_id id={0};
    int rc=nas_lwip_bridge_open(slot,type,&id);
    if(rc)return rc;
    sockets[slot]=(struct port_socket){.owned=true,.type=type,
        .id=id,.command=PORT_OPEN};
    return 0;
}

int nas_lwip_socket_port_adopt(struct nas_lwip_bridge_id accepted) {
    unsigned slot=accepted.slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)
        return -EBADF;
    if(sockets[slot].owned)return -EBUSY;
    if(!nas_lwip_bridge_alive(accepted))return -EBADF;
    sockets[slot]=(struct port_socket){.owned=true,.connected=true,
        .type=NAS_LWIP_BRIDGE_TCP,.id=accepted,.command=PORT_IDLE};
    return 0;
}

int nas_lwip_socket_port_begin(unsigned slot,
                              const struct nas_lwip_bridge_request *request) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(!request || request->length)return -EINVAL;
    switch(request->op) {
    case NAS_LWIP_BRIDGE_BIND:case NAS_LWIP_BRIDGE_CONNECT:
    case NAS_LWIP_BRIDGE_LISTEN:case NAS_LWIP_BRIDGE_ACCEPT:
    case NAS_LWIP_BRIDGE_GETNAME:case NAS_LWIP_BRIDGE_SHUTDOWN:
    case NAS_LWIP_BRIDGE_SETREUSEADDR:case NAS_LWIP_BRIDGE_SETBROADCAST:
    case NAS_LWIP_BRIDGE_SETNODELAY:case NAS_LWIP_BRIDGE_BIND_IF:break;
    default:return -EINVAL;
    }
    int rc=check(s);
    if(rc)return rc;
    if(s->connect_result_pending) {
        const struct nas_lwip_bridge_request *old=&s->control_request;
        return request->op==NAS_LWIP_BRIDGE_CONNECT &&
               old->address.ipv4==request->address.ipv4 &&
               old->address.port==request->address.port ? -EALREADY:-EBUSY;
    }
    if(s->queued_control || s->command==PORT_CONTROL ||
       s->command==PORT_CONTROL_DONE) {
        /* Never create two Task waiters for one consumable result. The
         * original Task resumes by slot, not by replaying this request. */
        const struct nas_lwip_bridge_request *old=&s->control_request;
        return old->op==request->op && old->address.ipv4==request->address.ipv4 &&
               old->address.port==request->address.port &&
               old->name_peer==request->name_peer &&
               old->shutdown_how==request->shutdown_how &&
               old->reuseaddr==request->reuseaddr &&
               old->broadcast==request->broadcast &&
               old->nodelay==request->nodelay &&
               old->if_index==request->if_index &&
               old->accept_slot==request->accept_slot &&
               old->accept_lease_token==request->accept_lease_token &&
               old->backlog==request->backlog ? -EALREADY:-EBUSY;
    }
    if(s->command==PORT_OPEN) {
        s->control_request=*request;
        s->control_request.bytes=NULL;
        s->queued_control=true;
        return 0;
    }
    const bool deferred_control=request->op==NAS_LWIP_BRIDGE_SHUTDOWN ||
                                request->op==NAS_LWIP_BRIDGE_GETNAME ||
                                request->op==NAS_LWIP_BRIDGE_SETREUSEADDR ||
                                request->op==NAS_LWIP_BRIDGE_SETBROADCAST ||
                                request->op==NAS_LWIP_BRIDGE_SETNODELAY ||
                                request->op==NAS_LWIP_BRIDGE_BIND_IF;
    if(deferred_control &&
       (s->command==PORT_TX || s->command==PORT_RX || s->tx_length)) {
        s->control_request=*request;
        s->control_request.bytes=NULL;
        s->queued_control=true;
        return 0;
    }
    if(s->command!=PORT_IDLE || s->tx_length ||
       (s->rx_valid && !deferred_control))
        return -EBUSY;
    rc=nas_lwip_bridge_submit(s->id,request);
    if(!rc) {
        s->control_request=*request;
        s->control_request.bytes=NULL;
        s->command=PORT_CONTROL;
    }
    return rc;
}

int nas_lwip_socket_port_complete(unsigned slot,
                                 struct nas_lwip_bridge_result *result) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(!result)return -EINVAL;
    int rc=check(s);
    if(rc)return rc;
    if(s->connect_result_pending) {
        if(s->connecting)return 0;
        result->status=s->connect_result;
        s->connect_result_pending=false;
        s->control_request=(struct nas_lwip_bridge_request){0};
        return 1;
    }
    if(s->command!=PORT_CONTROL_DONE)return s->command==PORT_CONTROL ? 0:-EINVAL;
    *result=s->control_result;
    if(s->control_request.op==NAS_LWIP_BRIDGE_LISTEN && !result->status)
        s->listening=true;
    if(s->control_request.op==NAS_LWIP_BRIDGE_SHUTDOWN && !result->status) {
        unsigned how=s->control_request.shutdown_how;
        if(how==0U || how==2U)s->shut_read=true;
        if(how==1U || how==2U)s->shut_write=true;
    }
    s->control_result=(struct nas_lwip_bridge_result){0};
    s->control_request=(struct nas_lwip_bridge_request){0};
    s->command=PORT_IDLE;
    return 1;
}

int nas_lwip_socket_port_take_error(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    (void)check(s);
    int error=s->tx_error;
    s->tx_error=0;
    return error;
}

int64_t nas_lwip_socket_sendto(unsigned slot,const uint8_t *bytes,
                               uint16_t length,
                               struct nas_lwip_bridge_address destination) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(s->type!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    if(length>NAS_LWIP_BRIDGE_DATA)return -EMSGSIZE;
    /* A zero-length UDP datagram needs an explicit pending-data marker;
     * leave it unsupported until that state is represented faithfully. */
    if(!length)return -EOPNOTSUPP;
    if(!bytes)return -EINVAL;
    int rc=check(s);
    if(rc)return rc;
    if(s->tx_error){rc=s->tx_error;s->tx_error=0;return rc;}
    if(s->command!=PORT_IDLE || s->tx_length || s->queued_control)
        return -EAGAIN;
    if(!destination.port && !s->connected)return -EDESTADDRREQ;
    memcpy(s->tx,bytes,length);
    s->tx_destination=destination;
    s->tx_length=length;s->tx_position=0;s->tx_retry_at=0;
    submit_tx(s);
    if(s->command!=PORT_TX){
        s->tx_length=s->tx_position=0;
        return s->tx_error ? s->tx_error:-EAGAIN;
    }
    return length; /* copied into one owned command, not network delivery */
}

int64_t nas_lwip_socket_recvfrom(unsigned slot,uint8_t *bytes,
                                 uint16_t capacity,
                                 struct nas_lwip_bridge_address *source,
                                 bool *truncated) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(s->type!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    int rc=check(s);
    if(rc)return rc;
    if(s->rx_error){rc=s->rx_error;s->rx_error=0;return rc;}
    if(!s->rx_valid){
        if(s->command==PORT_IDLE)submit_rx(s);
        return -EAGAIN;
    }
    unsigned n=s->rx_length<capacity?s->rx_length:capacity;
    if(n && !bytes)return -EINVAL;
    if(n)memcpy(bytes,s->rx,n);
    if(source)*source=s->rx_source;
    if(truncated)*truncated=s->rx_truncated || n<s->rx_length;
    s->rx_valid=false; /* datagrams consume as a unit, including truncation */
    s->rx_position=s->rx_length=0;
    return (int64_t)n;
}

static uint64_t read_plan(unsigned slot,uint64_t requested) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    int rc=check(s);
    if(rc)return (uint64_t)(int64_t)rc;
    if(s->shut_read)return 0;
    if(s->listening)return (uint64_t)-ENOTCONN;
    if(s->type==NAS_LWIP_BRIDGE_TCP && !s->connected && !s->connecting)
        return (uint64_t)-ENOTCONN;
    if(s->rx_valid) {
        uint64_t left=(uint64_t)s->rx_length-s->rx_position;
        return left<requested ? left:requested;
    }
    if(s->rx_error) {
        rc=s->rx_error;s->rx_error=0;
        return (uint64_t)(int64_t)rc;
    }
    if(s->rx_eof)return 0;
    if(s->command==PORT_IDLE)submit_rx(s);
    return (uint64_t)(int64_t)((nas_open_files[slot].status_flags&0x800ULL) ?
                               -EAGAIN:-EOPNOTSUPP);
}

static uint64_t read_bytes(unsigned slot,uint8_t *dst,uint64_t count) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!s->rx_valid)return count ? (uint64_t)-EAGAIN:0;
    uint64_t available=(uint64_t)s->rx_length-s->rx_position;
    if(count>available)count=available;
    if(count && !dst)return (uint64_t)-EINVAL;
    if(count)memcpy(dst,s->rx+s->rx_position,(size_t)count);
    s->rx_position+=(uint16_t)count;
    if(s->type==NAS_LWIP_BRIDGE_UDP || s->rx_position==s->rx_length)
        s->rx_valid=false;
    return count;
}

/* Read without advancing the shared socket's RX cursor (MSG_PEEK). A
 * concurrent dup/fork reader still shares this one OpenFile buffer. */
static uint64_t peek_bytes(unsigned slot,uint8_t *dst,uint64_t count,
                           uint64_t offset) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(s->type!=NAS_LWIP_BRIDGE_TCP)return (uint64_t)-EOPNOTSUPP;
    if(!s->rx_valid)return count ? (uint64_t)-EAGAIN:0;
    uint64_t available=(uint64_t)s->rx_length-s->rx_position;
    if(offset>available)return (uint64_t)-EINVAL;
    if(count>available-offset)count=available-offset;
    if(count && !dst)return (uint64_t)-EINVAL;
    if(count)memcpy(dst,s->rx+s->rx_position+offset,(size_t)count);
    return count;
}

static uint64_t write_plan(unsigned slot,uint64_t requested) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    if(s->type==NAS_LWIP_BRIDGE_UDP && requested>NAS_LWIP_BRIDGE_DATA)
        return (uint64_t)-EMSGSIZE; /* one write must remain one datagram */
    int rc=check(s);
    if(rc)return (uint64_t)(int64_t)rc;
    if(send_shutdown_requested(s))return (uint64_t)-EPIPE;
    if(s->tx_error) {
        rc=s->tx_error;s->tx_error=0;
        return (uint64_t)(int64_t)rc;
    }
    if(s->connecting)return (uint64_t)(int64_t)(
        (nas_open_files[slot].status_flags&0x800ULL) ? -EAGAIN:-EOPNOTSUPP);
    if(!s->connected)return (uint64_t)-ENOTCONN;
    if(s->tx_length || s->command!=PORT_IDLE)
        return (uint64_t)(int64_t)((nas_open_files[slot].status_flags&0x800ULL) ?
                                   -EAGAIN:-EOPNOTSUPP);
    return requested<NAS_LWIP_BRIDGE_DATA ? requested:NAS_LWIP_BRIDGE_DATA;
}

static uint64_t write_bytes(unsigned slot,const uint8_t *src,uint64_t count) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!count)return 0;
    if(send_shutdown_requested(s))return (uint64_t)-EPIPE;
    if(!src || count>NAS_LWIP_BRIDGE_DATA)return (uint64_t)-EINVAL;
    if(s->command!=PORT_IDLE || s->tx_length)return (uint64_t)-EAGAIN;
    memcpy(s->tx,src,(size_t)count);
    s->tx_destination=(struct nas_lwip_bridge_address){0};
    s->tx_length=(uint16_t)count;s->tx_position=0;s->tx_retry_at=0;
    submit_tx(s);
    if(s->command!=PORT_TX) {
        s->tx_length=s->tx_position=0;
        return (uint64_t)(int64_t)(s->tx_error ? s->tx_error:-EAGAIN);
    }
    return count;
}

static uint16_t poll_description(unsigned slot,uint16_t events) {
    struct port_socket *s=socket_at(slot);
    if(!s)return PORT_POLLERR;
    if(check(s))return PORT_POLLERR;
    if(s->tx_length && s->command==PORT_IDLE)submit_tx(s);
    if((events&PORT_POLLIN) && s->command==PORT_IDLE)submit_rx(s);
    /* An RX request started above can complete on a subsequent native
     * scheduling slice, never synchronously in this poll callback. */
    uint16_t ready=0;
    if(s->rx_valid || s->rx_eof || s->rx_error || accept_finished(s))
        ready|=PORT_POLLIN;
    if(s->shut_read)ready|=PORT_POLLIN|PORT_POLLHUP;
    if(s->listening && (events&PORT_POLLIN)) {
        if(s->prefetch_owned) {
            /* A native ACCEPT that lost a race with another consumer of
             * lwIP's backlog is not readable. Retire the reserved slot and
             * let the next callback start a fresh attempt. */
            if(accept_finished(s) && s->control_result.status==-EAGAIN) {
                struct nas_fd_slot_lease ignored={0};
                struct nas_lwip_bridge_id child={0};
                struct nas_lwip_bridge_address peer={0};
                (void)nas_lwip_socket_accept_prefetched(slot,&ignored,
                                                         &child,&peer);
                ready&=(uint16_t)~PORT_POLLIN;
            }
        } else if(!s->accept_owned && !s->accept_cancelling &&
                  s->command==PORT_IDLE) {
            if(nas_open_files[slot].status_flags&0x800ULL) {
                /* Native lwIP only exposes pending-backlog indication,
                 * whereas userspace select/poll must mean a nonblocking
                 * accept can actually harvest a completed child. */
                int rc=nas_lwip_socket_accept_prefetch_start(slot);
                if(rc && rc!=-EAGAIN && rc!=-EBUSY && rc!=-EALREADY)
                    ready|=PORT_POLLERR;
            } else if(nas_lwip_bridge_listener_readable(s->id)) {
                ready|=PORT_POLLIN;
            }
        }
    }
    if(s->rx_eof)ready|=PORT_POLLHUP;
    if(s->tx_error || s->rx_error)ready|=PORT_POLLERR;
    if((s->connected || s->type==NAS_LWIP_BRIDGE_UDP) && !s->connecting &&
       !send_shutdown_requested(s) &&
       !s->tx_length &&
       s->command==PORT_IDLE)
        ready|=PORT_POLLOUT;
    return ready&(uint16_t)(events|PORT_POLLERR|PORT_POLLHUP);
}

static uint16_t poll_fd(uint64_t fd,uint16_t events) {
    if(!nas_current_task || !nas_current_task->fds || fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_SOCKET)return 0;
    return poll_description(nas_current_task->fds->entries[fd].slot,events);
}

static void release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !sockets[slot].owned)return;
    struct port_socket *s=&sockets[slot];
    struct nas_lwip_bridge_id id=s->id;
    if(s->prefetch_owned) {
        /* nas_fd_close has already set this listener's refcount to zero.
         * Use the internal cancellation path, which does not call
         * socket_at(), while the cached bridge identity still exists.
         * The native worker must see cancellation before lease invalidity. */
        struct nas_fd_slot_lease lease=s->prefetch_lease;
        (void)nas_lwip_socket_accept_cancel_internal(s,lease);
        (void)nas_fd_slot_lease_cancel(lease);
        s->prefetch_owned=false;
    }
    /* Once port finish() took the bridge result, the listener no longer
     * owns that accepted child in the native bridge's result field. */
    if(s->command==PORT_CONTROL_DONE &&
       s->control_request.op==NAS_LWIP_BRIDGE_ACCEPT &&
       s->control_result.accepted.generation)
        (void)nas_lwip_bridge_release(s->control_result.accepted);
    /* `PORT_TX` already owns the suffix inside bridge.c, even if its
     * completion has not yet been taken here. If a previous partial SEND
     * completed, `PORT_IDLE` keeps the remaining copied suffix in this
     * File. Transfer that suffix before clearing the File, so close() does
     * not silently forget bytes previously accepted by write(). */
    const uint16_t tail=s->tx_length>=s->tx_position ?
                        s->tx_length-s->tx_position:0;
    const bool bridge_tx=s->command==PORT_TX;
    int rc=nas_lwip_bridge_release_with_tail(
        id,tail && !bridge_tx ? s->tx+s->tx_position:NULL,
        tail && !bridge_tx ? tail:0);
    /* A malformed/overlapping bridge state must never orphan its native
     * Netconn after the File's final refcount has gone away. */
    if(rc)(void)nas_lwip_bridge_release(id);
    sockets[slot]=(struct port_socket){0};
}

static const struct nas_file_ops ops={
    .read_plan=read_plan,.write_plan=write_plan,
    .read=read_bytes,.peek=peek_bytes,.write=write_bytes,
    .read_finalize_zero=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .sends_sigpipe=true,
    .poll=poll_fd,.poll_description=poll_description,
    .release=release
};
const struct nas_file_ops *nas_lwip_socket_port_ops(void){return &ops;}
bool nas_lwip_socket_slot_reusable(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && !sockets[slot].owned &&
           nas_lwip_bridge_slot_available(slot);
}

int nas_lwip_socket_open(unsigned slot,enum nas_lwip_bridge_type type) {
    return nas_lwip_socket_port_open(slot,type);
}
int nas_lwip_socket_open_ready(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    int rc=check(s);
    if(rc)return rc;
    return s->command==PORT_OPEN ? 0:1;
}
bool nas_lwip_socket_is_slot(unsigned slot) {
    return socket_at(slot)!=NULL;
}
int nas_lwip_socket_type(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    return s ? (int)s->type:-EBADF;
}
int nas_lwip_socket_buffer_capacity(unsigned slot,bool sending) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(s->type!=NAS_LWIP_BRIDGE_TCP)return -ENOPROTOOPT;
    /* Official lwIP's configured transport bounds, never a made-up
     * Linux SO_SNDBUF or SO_RCVBUF capacity. */
    return sending ? TCP_SND_BUF:TCP_WND;
}
bool nas_lwip_socket_is_connected(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    return s && s->connected && !s->connecting;
}
int nas_lwip_socket_peer_snapshot(unsigned slot,
                                  struct nas_lwip_bridge_address *out) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(!out)return -EINVAL;
    if(s->type!=NAS_LWIP_BRIDGE_TCP)return -EOPNOTSUPP;
    int rc=check(s);
    if(rc)return rc;
    if(!s->connected || s->connecting || !s->peer_known)return -ENOTCONN;
    *out=s->peer_address;
    return 0;
}
int nas_lwip_socket_port_set_peer(unsigned slot,
                                  struct nas_lwip_bridge_address peer) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(s->type!=NAS_LWIP_BRIDGE_TCP || !s->connected || s->listening ||
       s->connecting || !peer.port)return -EINVAL;
    /* Accepted TCP's actual peer was obtained by the native bridge's
     * netconn_getaddr before adoption. TCP peers cannot subsequently
     * change, including across read/recvfrom calls. */
    if(s->peer_known)
        return s->peer_address.ipv4==peer.ipv4 &&
               s->peer_address.port==peer.port ? 0:-EALREADY;
    s->peer_known=true;
    s->peer_address=peer;
    return 0;
}
bool nas_lwip_socket_is_listening(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    return s && s->listening;
}
int nas_lwip_socket_bind(unsigned slot,struct nas_lwip_bridge_address address) {
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_BIND,.address=address
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_connect(unsigned slot,struct nas_lwip_bridge_address address) {
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_CONNECT,.address=address
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_listen(unsigned slot,unsigned backlog) {
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_LISTEN,
        .backlog=(uint16_t)(backlog>UINT16_MAX ? UINT16_MAX:backlog)
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_getname_begin(unsigned slot,bool peer) {
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_GETNAME,.name_peer=peer
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_getname_complete(unsigned slot,
                                     struct nas_lwip_bridge_address *address) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(!address)return -EINVAL;
    if(s->control_request.op!=NAS_LWIP_BRIDGE_GETNAME)return -EINVAL;
    struct nas_lwip_bridge_result result={0};
    int rc=nas_lwip_socket_port_complete(slot,&result);
    if(rc<=0)return rc;
    if(result.status)return result.status;
    *address=result.peer;
    return 1;
}
int nas_lwip_socket_shutdown(unsigned slot,unsigned how) {
    if(how>2U)return -EINVAL;
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_SHUTDOWN,.shutdown_how=(uint8_t)how
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_setreuseaddr(unsigned slot,bool enabled) {
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_SETREUSEADDR,.reuseaddr=enabled
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_setbroadcast(unsigned slot,bool enabled) {
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_SETBROADCAST,.broadcast=enabled
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_bind_if(unsigned slot,unsigned index) {
    if(!index || index>UINT8_MAX)return -EINVAL;
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_BIND_IF,.if_index=(uint8_t)index
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_bound_ifindex(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    (void)check(s);
    return (int)s->bound_ifindex;
}
int nas_lwip_socket_tcp_nodelay(unsigned slot,bool write,bool *enabled) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(!enabled)return -EINVAL;
    if(s->type!=NAS_LWIP_BRIDGE_TCP)return -EOPNOTSUPP;
    if(!write) {
        *enabled=s->deferred_nodelay ? s->deferred_nodelay_value:
                 s->tcp_nodelay;
        return 0;
    }
    /* Linux permits TCP_NODELAY immediately after a nonblocking connect.
     * The pending CONNECT owns the one-shot bridge result, so queuing the
     * option as another waiter would corrupt its result ownership. Retain
     * one deferred mutation on this OpenFile and apply it on tcpip_thread. */
    if((nas_open_files[slot].status_flags&0x800ULL) &&
       ((s->command==PORT_CONTROL &&
         s->control_request.op==NAS_LWIP_BRIDGE_CONNECT) ||
        s->command==PORT_CONNECT_PROBE || s->connecting ||
        s->deferred_nodelay)) {
        s->deferred_nodelay=true;
        s->deferred_nodelay_value=*enabled;
        return 1; /* owned deferred request, no Task waiter */
    }
    const struct nas_lwip_bridge_request req={
        .op=NAS_LWIP_BRIDGE_SETNODELAY,.nodelay=*enabled
    };
    return nas_lwip_socket_port_begin(slot,&req);
}
int nas_lwip_socket_ip_recverr(unsigned slot,bool write,bool *enabled) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    if(!enabled)return -EINVAL;
    if(s->type!=NAS_LWIP_BRIDGE_UDP)return -EOPNOTSUPP;
    /* Maintain the Linux option's File identity. Native Netconn already
     * exposes synchronous UDP errors through SO_ERROR; no MSG_ERRQUEUE is
     * synthesized, and no nonexistent ICMP metadata is fabricated. */
    if(write)s->ip_recverr=*enabled;
    else *enabled=s->ip_recverr;
    return 0;
}
int nas_lwip_socket_control_ready(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    if(!s)return -EBADF;
    int rc=check(s);
    if(rc)return rc;
    if(s->connect_result_pending)return s->connecting ? 0:1;
    return s->command==PORT_CONTROL_DONE ? 1:0;
}
int nas_lwip_socket_control_finish(unsigned slot) {
    struct nas_lwip_bridge_result result={0};
    int rc=nas_lwip_socket_port_complete(slot,&result);
    return rc==1 ? result.status : rc==0 ? -EINPROGRESS:rc;
}

static bool lease_matches(const struct port_socket *s,
                          struct nas_fd_slot_lease lease) {
    return s->accept_owned && s->accept_lease.slot==lease.slot &&
           s->accept_lease.token==lease.token && lease.token!=0;
}

int nas_lwip_socket_accept_begin(unsigned listener_slot,
                                 struct nas_fd_slot_lease lease) {
    struct port_socket *s=socket_at(listener_slot);
    if(!s)return -EBADF;
    if(!lease.token || lease.slot>=NAS_OPEN_FILE_LIMIT)return -EBADF;
    int rc=check(s);
    if(rc)return rc;
    if(s->type!=NAS_LWIP_BRIDGE_TCP || !s->listening)return -EINVAL;
    if(s->accept_cancelling)return -EBUSY; /* cancellation still draining */
    if(s->accept_owned)return -EALREADY; /* a different Task owns result */
    if(!nas_lwip_bridge_listener_readable(s->id))return -EAGAIN;
    struct nas_lwip_bridge_request request={
        .op=NAS_LWIP_BRIDGE_ACCEPT,
        .accept_slot=(uint16_t)lease.slot,
        .accept_lease_token=lease.token
    };
    rc=nas_lwip_socket_port_begin(listener_slot,&request);
    if(rc)return rc;
    s->accept_owned=true;
    s->accept_lease=lease;
    return 0;
}

int nas_lwip_socket_accept_complete(unsigned listener_slot,
                                    struct nas_fd_slot_lease lease,
                                    struct nas_lwip_bridge_id *accepted,
                                    struct nas_lwip_bridge_address *peer) {
    struct port_socket *s=socket_at(listener_slot);
    if(!s)return -EBADF;
    if(!accepted || !peer)return -EINVAL;
    if(!lease_matches(s,lease))return -EINVAL;
    struct nas_lwip_bridge_result result={0};
    int rc=nas_lwip_socket_port_complete(listener_slot,&result);
    if(rc==0)return 0;
    if(rc<0) {
        s->accept_owned=false;
        s->accept_lease=(struct nas_fd_slot_lease){0};
        return rc;
    }
    s->accept_owned=false;
    s->accept_lease=(struct nas_fd_slot_lease){0};
    if(result.status)return result.status;
    if(!result.accepted.generation)return -EIO;
    *accepted=result.accepted;
    *peer=result.peer;
    return 1;
}

static int nas_lwip_socket_accept_cancel_internal(
    struct port_socket *s,struct nas_fd_slot_lease lease) {
    if(!lease_matches(s,lease))return -EINVAL;
    /* Do not call check() before determining whether the consumer already
     * took a completed child from the native result; bridge_cancel_accept
     * cannot cancel a result that the port has already consumed. */
    if(s->queued_control &&
       s->control_request.op==NAS_LWIP_BRIDGE_ACCEPT) {
        s->queued_control=false;
        s->control_request=(struct nas_lwip_bridge_request){0};
        s->accept_owned=false;
        s->accept_lease=(struct nas_fd_slot_lease){0};
        return 0;
    }
    if(s->command==PORT_CONTROL_DONE &&
       s->control_request.op==NAS_LWIP_BRIDGE_ACCEPT) {
        if(s->control_result.accepted.generation)
            (void)nas_lwip_bridge_release(s->control_result.accepted);
        s->control_result=(struct nas_lwip_bridge_result){0};
        s->control_request=(struct nas_lwip_bridge_request){0};
        s->command=PORT_IDLE;
        s->accept_owned=false;
        s->accept_lease=(struct nas_fd_slot_lease){0};
        return 0;
    }
    if(s->command!=PORT_CONTROL ||
       s->control_request.op!=NAS_LWIP_BRIDGE_ACCEPT)
        return -EALREADY;
    int rc=nas_lwip_bridge_cancel_accept(s->id,&lease);
    if(rc)return rc;
    s->accept_owned=false;
    s->accept_lease=(struct nas_fd_slot_lease){0};
    s->accept_cancelling=true;
    return 0;
}

int nas_lwip_socket_accept_cancel(unsigned listener_slot,
                                  struct nas_fd_slot_lease lease) {
    struct port_socket *s=socket_at(listener_slot);
    return s ? nas_lwip_socket_accept_cancel_internal(s,lease):-EBADF;
}

int nas_lwip_socket_accept_prefetch_start(unsigned listener_slot) {
    struct port_socket *s=socket_at(listener_slot);
    if(!s)return -EBADF;
    int rc=check(s);
    if(rc)return rc;
    if(s->type!=NAS_LWIP_BRIDGE_TCP || !s->listening)return -EINVAL;
    if(s->prefetch_owned)return 0;
    if(s->accept_owned)return -EALREADY;
    if(s->accept_cancelling || s->command!=PORT_IDLE)return -EAGAIN;
    if(!nas_lwip_bridge_listener_readable(s->id))return -EAGAIN;

    /* The ticket is an ordinary global OpenFile lease; it remains alive
     * after the Task running poll/select returns. The bridge checks its
     * identity before and after netconn_accept. */
    struct nas_fd_slot_lease lease={0};
    rc=nas_fd_slot_lease_begin(&lease,"lwip accept prefetch");
    if(rc)return rc;
    rc=nas_lwip_socket_accept_begin(listener_slot,lease);
    if(rc) {
        (void)nas_fd_slot_lease_cancel(lease);
        return rc;
    }
    s->prefetch_owned=true;
    s->prefetch_lease=lease;
    return 0;
}

int nas_lwip_socket_accept_prefetched(unsigned listener_slot,
                                      struct nas_fd_slot_lease *lease,
                                      struct nas_lwip_bridge_id *accepted,
                                      struct nas_lwip_bridge_address *peer) {
    struct port_socket *s=socket_at(listener_slot);
    if(!s)return -EBADF;
    if(!lease || !accepted || !peer)return -EINVAL;
    if(!s->prefetch_owned)return 0;
    struct nas_fd_slot_lease ticket=s->prefetch_lease;
    int rc=nas_lwip_socket_accept_complete(listener_slot,ticket,
                                           accepted,peer);
    if(rc==0)return -EAGAIN;

    /* The listener no longer owns this ticket. On successful ACCEPT the
     * syscall gets both the real leased slot and accepted native identity;
     * only its subsequent commit publishes the child FD to userspace. */
    s->prefetch_owned=false;
    s->prefetch_lease=(struct nas_fd_slot_lease){0};
    if(rc==1) {
        *lease=ticket;
        return 1;
    }
    (void)nas_fd_slot_lease_cancel(ticket);
    return rc;
}
