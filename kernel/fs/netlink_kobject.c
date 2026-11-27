/* Linux NETLINK_KOBJECT_UEVENT (15) monitor endpoint.
 * Bound subscriptions describe which real multicast group would be heard.
 * This code does not impersonate eudev's user-space group and never emits
 * a synthetic DEVICE=add event for already-enumerated devices. */
#include "netlink_kobject.h"
#include "netlink_common.h"
#include "file_ops.h"
#include "../errno.h"
#include "../fd.h"

#define KOBJECT_PROTOCOL 15U
#define KOBJECT_KERNEL_GROUP 1U
#define KOBJECT_UDEV_GROUP 2U
#define KOBJECT_SUPPORTED_GROUPS (KOBJECT_KERNEL_GROUP|KOBJECT_UDEV_GROUP)
#define KOBJECT_SNDBUF_MIN 2048U
#define KOBJECT_SNDBUF_MAX 262144U
#define KOBJECT_RCVBUF_MIN 2048U
#define KOBJECT_RCVBUF_MAX 262144U
#define POLL_OUT 4U
#define POLL_ERR 8U

struct kobject_socket {
    struct nas_nl_endpoint ep;
    uint32_t groups;
    bool passcred;
};
static const struct nas_file_ops ops;
static uint32_t next_port=UINT32_C(0x70000000);
static struct kobject_socket *socket_at(unsigned slot) {
    return (struct kobject_socket *)nas_nl_private(slot,&ops);
}
static struct kobject_socket *live(unsigned slot) {
    struct kobject_socket *s=socket_at(slot);
    return s && nas_nl_live(slot,&s->ep)?s:NULL;
}
bool nas_netlink_kobject_is_slot(unsigned slot) {return live(slot)!=NULL;}
int nas_netlink_kobject_open(unsigned slot,uint32_t owner_pid) {
    void *memory=NULL;
    int rc=nas_nl_socket_alloc(slot,&ops,sizeof(struct kobject_socket),&memory);
    if(rc)return rc;
    struct kobject_socket *s=(struct kobject_socket *)memory;
    *s=(struct kobject_socket){
        .ep={.owned=true,.owner_pid=owner_pid,
             .send_quota=NAS_NETLINK_MAX_DATAGRAM,
             .receive_quota=KOBJECT_RCVBUF_MIN}};
    return 0;
}
int nas_netlink_kobject_protocol(unsigned slot) {
    return live(slot)?(int)KOBJECT_PROTOCOL:-EBADF;
}
static bool port_used(uint32_t port,unsigned protocol) {
    (void)protocol;
    for(unsigned i=0;i<NAS_OPEN_FILE_LIMIT;i++) {
        struct kobject_socket *s=socket_at(i);
        if(s && s->ep.owned && s->ep.bound && s->ep.port_id==port)
            return true;
    }
    return false;
}
int nas_netlink_kobject_bind(unsigned slot,const uint8_t *address,unsigned length) {
    struct kobject_socket *s=live(slot);
    if(!s)return -EBADF;
    uint32_t port=0,groups=0;
    int rc=nas_nl_parse_address(address,length,&port,&groups);
    if(rc)return rc;
    if(groups&~KOBJECT_SUPPORTED_GROUPS)return -EINVAL;
    rc=nas_nl_bind_port(&s->ep.bound,&s->ep.port_id,s->ep.owner_pid,
                        port,&next_port,KOBJECT_PROTOCOL,port_used);
    if(!rc)s->groups=groups;
    return rc;
}
int nas_netlink_kobject_getsockname(unsigned slot,uint8_t address[12]) {
    struct kobject_socket *s=live(slot);
    return nas_nl_name(s?&s->ep:NULL,address,s?s->groups:0);
}
int nas_netlink_kobject_set_sndbuf(unsigned slot,uint32_t requested) {
    struct kobject_socket *s=live(slot);
    return nas_nl_set_quota(s?&s->ep:NULL,false,requested,
                            KOBJECT_SNDBUF_MIN,KOBJECT_SNDBUF_MAX);
}
int nas_netlink_kobject_get_sndbuf(unsigned slot,uint32_t *bytes) {
    struct kobject_socket *s=live(slot);
    return nas_nl_get_quota(s?&s->ep:NULL,false,bytes);
}
int nas_netlink_kobject_set_rcvbuf(unsigned slot,uint32_t requested) {
    struct kobject_socket *s=live(slot);
    return nas_nl_set_quota(s?&s->ep:NULL,true,requested,
                            KOBJECT_RCVBUF_MIN,KOBJECT_RCVBUF_MAX);
}
int nas_netlink_kobject_get_rcvbuf(unsigned slot,uint32_t *bytes) {
    struct kobject_socket *s=live(slot);
    return nas_nl_get_quota(s?&s->ep:NULL,true,bytes);
}
int nas_netlink_kobject_set_passcred(unsigned slot,bool value) {
    struct kobject_socket *s=live(slot);
    if(!s)return -EBADF;
    s->passcred=value;
    return 0;
}
int nas_netlink_kobject_get_passcred(unsigned slot,bool *value) {
    struct kobject_socket *s=live(slot);
    if(!s)return -EBADF;
    if(!value)return -EINVAL;
    *value=s->passcred;
    return 0;
}
int64_t nas_netlink_kobject_sendto(unsigned slot,const uint8_t *bytes,
    unsigned length,const uint8_t *destination,unsigned destination_length) {
    (void)bytes;(void)length;(void)destination;(void)destination_length;
    return live(slot)?-EOPNOTSUPP:-EBADF;
}
int64_t nas_netlink_kobject_recvmsg(unsigned slot,uint8_t *destination,
    unsigned capacity,unsigned flags,uint8_t source[12],
    uint32_t *source_length,uint32_t *msg_flags) {
    (void)source;(void)source_length;(void)msg_flags;
    if(!live(slot))return -EBADF;
    int rc=nas_nl_recv_check(capacity,destination,flags);
    return rc?rc:-EAGAIN;
}
static uint64_t read_plan(unsigned slot,uint64_t requested) {
    if(!live(slot))return (uint64_t)-EBADF;
    return requested?(uint64_t)-EAGAIN:0;
}
static uint64_t read_bytes(unsigned slot,uint8_t *dst,uint64_t count) {
    return (uint64_t)nas_netlink_kobject_recvmsg(slot,dst,(unsigned)count,
                                                  0,NULL,NULL,NULL);
}
static uint16_t poll_description(unsigned slot,uint16_t events) {
    return live(slot)?(uint16_t)(events&POLL_OUT):POLL_ERR;
}
static void release(unsigned slot) {
    nas_nl_socket_release(slot,&ops);
}
static const struct nas_file_ops ops={
    .read_plan=read_plan,.read=read_bytes,
    .io_wait_flags=NAS_IO_WAIT_INITIAL_EAGAIN|NAS_IO_WAIT_RETRY,
    .poll_description=poll_description,.release=release
};
const struct nas_file_ops *nas_netlink_kobject_ops(void){return &ops;}
