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

#define PORT_POLLIN 0x0001U
#define PORT_POLLOUT 0x0004U
#define PORT_POLLERR 0x0008U
#define PORT_POLLHUP 0x0010U
#define PORT_RETRY_NS UINT64_C(1000000)

enum port_command {
    PORT_IDLE=0, PORT_OPEN, PORT_CONTROL, PORT_CONTROL_DONE,
    PORT_RX, PORT_TX, PORT_CONNECT_PROBE, PORT_FAILED
};
struct port_socket {
    bool owned,connected,connecting,listening,queued_control;
    bool connect_result_pending;
    bool rx_valid,rx_eof,rx_truncated;
    enum nas_lwip_bridge_type type;
    enum port_command command;
    struct nas_lwip_bridge_id id;
    struct nas_lwip_bridge_result control_result;
    struct nas_lwip_bridge_request control_request;
    struct nas_lwip_bridge_address connect_address;
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
       s->command!=PORT_CONNECT_PROBE)return;
    struct nas_lwip_bridge_result result={0};
    int got=nas_lwip_bridge_take(s->id,&result);
    if(got==0)return;
    if(got<0) {
        s->failure=got;
        s->command=PORT_FAILED;
        return;
    }
    switch(s->command) {
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
            s->connect_result_pending=true;
            s->command=PORT_IDLE;
            if(result.status==0 || result.status==-EISCONN) {
                s->connected=true;
                s->connecting=false;
                s->connect_result=0;
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
    if(s->command!=PORT_IDLE || s->rx_valid || s->rx_eof || s->rx_error ||
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
    if(s->command==PORT_IDLE && s->queued_control) {
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
    if(!request || request->op<NAS_LWIP_BRIDGE_BIND ||
       request->op>NAS_LWIP_BRIDGE_ACCEPT ||
       request->length)return -EINVAL;
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
    if(s->command!=PORT_IDLE || s->tx_length || s->rx_valid)
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

static uint64_t read_plan(unsigned slot,uint64_t requested) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    int rc=check(s);
    if(rc)return (uint64_t)(int64_t)rc;
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

static uint64_t write_plan(unsigned slot,uint64_t requested) {
    struct port_socket *s=socket_at(slot);
    if(!s)return (uint64_t)-EBADF;
    if(!requested)return 0;
    int rc=check(s);
    if(rc)return (uint64_t)(int64_t)rc;
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
    if(!src || count>NAS_LWIP_BRIDGE_DATA)return (uint64_t)-EINVAL;
    if(s->command!=PORT_IDLE || s->tx_length)return (uint64_t)-EAGAIN;
    memcpy(s->tx,src,(size_t)count);
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
    if(s->rx_valid || s->rx_eof || s->rx_error ||
       (s->command==PORT_CONTROL_DONE &&
        s->control_request.op==NAS_LWIP_BRIDGE_ACCEPT))ready|=PORT_POLLIN;
    if(s->listening && nas_lwip_bridge_listener_readable(s->id))
        ready|=PORT_POLLIN;
    if(s->rx_eof)ready|=PORT_POLLHUP;
    if(s->tx_error || s->rx_error)ready|=PORT_POLLERR;
    if(s->connected && !s->connecting && !s->tx_length &&
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
    struct nas_lwip_bridge_id id=sockets[slot].id;
    sockets[slot]=(struct port_socket){0};
    (void)nas_lwip_bridge_release(id);
}

static const struct nas_file_ops ops={
    .read_plan=read_plan,.write_plan=write_plan,
    .read=read_bytes,.write=write_bytes,
    .read_finalize_zero=true,
    .io_wait_flags=NAS_IO_WAIT_INITIAL|NAS_IO_WAIT_RETRY,
    .sends_sigpipe=true,
    .poll=poll_fd,.poll_description=poll_description,
    .release=release
};
const struct nas_file_ops *nas_lwip_socket_port_ops(void){return &ops;}

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
bool nas_lwip_socket_is_connected(unsigned slot) {
    struct port_socket *s=socket_at(slot);
    return s && s->connected && !s->connecting;
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
