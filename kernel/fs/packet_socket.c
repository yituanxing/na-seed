/* Real AF_PACKET RX capture, with page-backed Linux TPACKET_V2 mmap.
 * Neither loopback packets nor synthetic traffic are created: a frame is
 * published only after the live Ethernet driver receives or accepts it.
 * Device/VMA lifetimes are independent of the owning File's final close. */
#include "packet_socket.h"
#include "file_ops.h"
#include "../fd.h"
#include "../errno.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../mm/frame.h"
#include "../drivers/port/net.h"
#include "../arch/arch.h"
#include "../time/clock.h"
#include "../../ports/lwip/network_boot.h"

#define PACKET_MAX_RING (8U*1024U*1024U)
#define PACKET_PAGE 4096U
#define PACKET_VERSION_V2 1U
#define PACKET_STATUS_USER 1U
#define PACKET_POLLIN 1U
#define PACKET_POLLOUT 4U
#define PACKET_ETH_ALL 3U

struct packet_ring {
    struct nas_vm_device device;
    uint64_t *pages;
    uint32_t page_count,refs,bytes,block_size,frame_size;
    uint32_t frames_per_block,frame_count,next;
};
struct packet_socket {
    struct nas_netdev *nic;
    struct packet_ring *ring;
    uint32_t ifindex,packets,drops,reserve;
    uint16_t protocol;
    uint8_t version;
    bool owned,bound;
};
static struct packet_socket sockets[NAS_OPEN_FILE_LIMIT];
static uint8_t observer_identity;
static unsigned socket_count;

static struct packet_socket *live(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT && sockets[slot].owned &&
           nas_open_files[slot].refcount ? &sockets[slot]:NULL;
}
bool nas_packet_socket_is_slot(unsigned slot) {return live(slot)!=NULL;}

static uint8_t *ring_pointer(const struct packet_ring *ring,uint32_t off) {
    if(!ring || off>=ring->bytes)return NULL;
    return (uint8_t *)(uintptr_t)(nas_hhdm_offset()+ring->pages[off/PACKET_PAGE]+off%PACKET_PAGE);
}
/* Linux PACKET_MMAP packs floor(block_size/frame_size) frames into each
 * page-aligned block. A final slack region is valid and is never a frame.
 * libpcap's snapshot-derived, 16-byte-aligned frame size is commonly NOT
 * page-aligned or an exact divisor of its power-of-two block size. */
static uint32_t ring_frame_offset(const struct packet_ring *ring,
                                  uint32_t frame_index) {
    return (frame_index/ring->frames_per_block)*ring->block_size+
           (frame_index%ring->frames_per_block)*ring->frame_size;
}
static void ring_copy(struct packet_ring *ring,uint32_t off,const uint8_t *src,uint32_t length) {
    while(length) {
        uint32_t available=PACKET_PAGE-off%PACKET_PAGE;
        if(available>length)available=length;
        memcpy(ring_pointer(ring,off),src,available);
        off+=available;src+=available;length-=available;
    }
}
static void ring_put(void *context) {
    struct packet_ring *ring=context;
    if(!ring || !ring->refs || --ring->refs)return;
    for(uint32_t i=0;i<ring->page_count;i++)
        if(ring->pages[i])nas_frame_put(ring->pages[i]);
    nas_heap_free(ring->pages);
    nas_heap_free(ring);
}
static bool ring_get(void *context) {
    struct packet_ring *ring=context;
    if(!ring || !ring->refs || ring->refs==UINT32_MAX)return false;
    ring->refs++;
    return true;
}
static int ring_page(void *context,uint64_t offset,uint64_t *frame) {
    struct packet_ring *ring=context;
    if(!ring || !frame || offset>=ring->bytes)return -EINVAL;
    *frame=ring->pages[offset/PACKET_PAGE];
    return 0;
}
/* tpacket2_hdr is 32 bytes; sockaddr_ll starts at aligned offset 32.
 * Linux netoff=ALIGN(TPACKET2_HDRLEN(52)+max(ETH_HLEN(14),16),16)+reserve,
 * macoff=netoff-ETH_HLEN. Data is the driver's complete Ethernet frame. */
static void publish(struct packet_socket *socket,const struct nas_netdev *nic,
                    const uint8_t *frame,uint32_t length,bool outgoing) {
    struct packet_ring *ring=socket->ring;
    if(!ring)return;
    socket->packets++;
    uint32_t frame_index=ring->next;
    uint32_t base=ring_frame_offset(ring,frame_index);
    uint32_t *status=(uint32_t *)(void *)ring_pointer(ring,base);
    if(__atomic_load_n(status,__ATOMIC_ACQUIRE)!=0) {
        socket->drops++;
        return;
    }
    uint16_t netoff=(uint16_t)(80U+socket->reserve);
    uint16_t macoff=(uint16_t)(netoff-14U);
    if((uint32_t)macoff>=ring->frame_size) {
        socket->drops++;
        return;
    }
    uint32_t copied=length;
    if(copied>ring->frame_size-macoff)copied=ring->frame_size-macoff;
    /* No frame can cross block boundaries. A 16-byte-aligned frame may
     * span PMM pages; ring_copy preserves those page transitions. */
    uint8_t header[32]={0};
    memcpy(header+4,&length,4);
    memcpy(header+8,&copied,4);
    memcpy(header+12,&macoff,2);
    memcpy(header+14,&netoff,2);
    uint64_t stamp=0;
    if(nas_time_realtime_ns(&stamp)) {
        uint32_t sec=(uint32_t)(stamp/1000000000ULL);
        uint32_t nsec=(uint32_t)(stamp%1000000000ULL);
        memcpy(header+16,&sec,4);memcpy(header+20,&nsec,4);
    }
    ring_copy(ring,base,header,sizeof(header));
    uint8_t addr[20]={0};
    uint16_t family=17,hatype=1;
    int32_t ifindex=(int32_t)socket->ifindex;
    memcpy(addr,&family,2);
    if(length>=14) {addr[2]=frame[12];addr[3]=frame[13];}
    memcpy(addr+4,&ifindex,4);
    memcpy(addr+8,&hatype,2);
    addr[10]=outgoing?4U:0U; /* PACKET_OUTGOING/PACKET_HOST */
    addr[11]=6;
    if(length>=12)memcpy(addr+12,frame+6,6);
    else memcpy(addr+12,nic->mac,6);
    ring_copy(ring,base+32,addr,sizeof(addr));
    ring_copy(ring,base+macoff,frame,copied);
    /* No write to a USER-owned frame. Metadata and payload precede the
     * release-store; libpcap acquires tp_status before reading this data. */
    __atomic_store_n(status,PACKET_STATUS_USER,__ATOMIC_RELEASE);
    ring->next=(frame_index+1U)%ring->frame_count;
}
static void packet_tap(void *identity,const struct nas_netdev *nic,
                       const uint8_t *frame,uint32_t length,bool outgoing) {
    if(identity!=&observer_identity || !nic || !frame || length<14)return;
    const uint16_t ether_type=(uint16_t)((unsigned)frame[12]<<8|frame[13]);
    for(unsigned slot=0;slot<NAS_OPEN_FILE_LIMIT;slot++) {
        struct packet_socket *socket=&sockets[slot];
        if(!socket->owned || !socket->bound || socket->nic!=nic ||
           !socket->protocol || (socket->protocol!=PACKET_ETH_ALL &&
                                 socket->protocol!=ether_type))continue;
        publish(socket,nic,frame,length,outgoing);
    }
}
int nas_packet_socket_open(unsigned slot,unsigned type,uint16_t protocol) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_open_files[slot].refcount)return -EBADF;
    if(type!=3U)return -ESOCKTNOSUPPORT;
    if(sockets[slot].owned)return -EBUSY;
    if(!socket_count) {
        int rc=nas_netdev_set_packet_tap(packet_tap,&observer_identity);
        if(rc)return rc;
    }
    socket_count++;
    /* Linux initializes packet sockets in V1. This deliberately supports
     * ring creation only after the caller negotiates actual V2 behavior. */
    sockets[slot]=(struct packet_socket){.owned=true,.protocol=protocol};
    return 0;
}
int nas_packet_socket_bind(unsigned slot,unsigned ifindex,uint16_t protocol) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(!ifindex)return -EINVAL;
    char name[NAS_LWIP_DEVICE_NAME_MAX]={0};
    int rc=nas_lwip_guest_ifname(ifindex,name);
    if(rc)return rc;
    struct nas_netdev *nic=nas_netdev_get(name);
    if(!nic)return -ENODEV;
    struct nas_netdev *old=socket->nic;
    socket->nic=nic;
    socket->ifindex=ifindex;
    socket->protocol=protocol;
    socket->bound=true;
    if(old)nas_netdev_put(old);
    return 0;
}
int nas_packet_socket_name(unsigned slot,unsigned *ifindex,uint16_t *protocol) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(ifindex)*ifindex=socket->ifindex;
    if(protocol)*protocol=socket->protocol;
    return 0;
}
int nas_packet_socket_version(unsigned slot,unsigned *version,bool change) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(!version)return -EINVAL;
    if(change) {
        if(*version!=PACKET_VERSION_V2)return -EINVAL;
        if(socket->ring)return -EBUSY;
        socket->version=(uint8_t)*version;
    } else *version=socket->version;
    return 0;
}
int nas_packet_socket_reserve(unsigned slot,unsigned *reserve,bool change) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(!reserve)return -EINVAL;
    if(change) {
        if(socket->ring)return -EBUSY;
        if(*reserve>128U)return -EINVAL;
        socket->reserve=*reserve;
    } else *reserve=socket->reserve;
    return 0;
}
int nas_packet_socket_ring(unsigned slot,const struct nas_packet_ring_request *req) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(socket->ring)return -EBUSY; /* mapped rings are never silently freed */
    if(!req || socket->version!=PACKET_VERSION_V2 ||
       req->block_size<PACKET_PAGE || req->block_size%PACKET_PAGE ||
       !req->block_nr || req->frame_size<128U ||
       (req->frame_size&15U) || req->frame_size>req->block_size ||
       (uint64_t)req->block_size*req->block_nr>PACKET_MAX_RING ||
       (uint64_t)(req->block_size/req->frame_size)*req->block_nr!=req->frame_nr ||
       !req->frame_nr || req->frame_nr>PACKET_MAX_RING/128U ||
       80U+socket->reserve>=req->frame_size)return -EINVAL;
    uint32_t size=req->block_size*req->block_nr;
    struct packet_ring *ring=nas_heap_calloc(1,sizeof(*ring));
    if(!ring)return -ENOMEM;
    ring->page_count=size/PACKET_PAGE;
    ring->pages=nas_heap_calloc(ring->page_count,sizeof(uint64_t));
    if(!ring->pages){nas_heap_free(ring);return -ENOMEM;}
    ring->refs=1;
    ring->bytes=size;
    ring->block_size=req->block_size;
    ring->frame_size=req->frame_size;
    ring->frames_per_block=req->block_size/req->frame_size;
    ring->frame_count=req->frame_nr;
    ring->device=(struct nas_vm_device){.context=ring,.extent_bytes=size,
                                       .get=ring_get,.put=ring_put,.page=ring_page};
    for(uint32_t i=0;i<ring->page_count;i++) {
        ring->pages[i]=nas_frame_alloc();
        if(!ring->pages[i]){ring_put(ring);return -ENOMEM;}
        /* PMM currently guarantees zero-initialized nas_frame_alloc. */
    }
    socket->ring=ring;
    return 0;
}
int nas_packet_socket_mmap_pin(unsigned slot,struct nas_vm_device **device) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(!device)return -EINVAL;
    if(!socket->ring)return -EINVAL;
    if(!ring_get(socket->ring))return -EBUSY;
    *device=&socket->ring->device;
    return 0;
}
int nas_packet_socket_stats(unsigned slot,struct nas_packet_stats *stats) {
    struct packet_socket *socket=live(slot);
    if(!socket)return -EBADF;
    if(!stats)return -EINVAL;
    stats->packets=socket->packets;
    stats->drops=socket->drops;
    socket->packets=socket->drops=0;
    return 0;
}
static void packet_release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !sockets[slot].owned)return;
    struct packet_socket socket=sockets[slot];
    sockets[slot]=(struct packet_socket){0};
    if(socket.nic)nas_netdev_put(socket.nic);
    if(socket.ring)ring_put(socket.ring);
    if(socket_count && !--socket_count)
        nas_netdev_set_packet_tap(NULL,NULL);
}
static uint16_t packet_poll(unsigned slot,uint16_t events) {
    struct packet_socket *socket=live(slot);
    if(!socket)return 8U; /* POLLERR */
    uint16_t ready=0;
    if(events&PACKET_POLLOUT)ready|=PACKET_POLLOUT;
    if((events&PACKET_POLLIN) && socket->ring) {
        struct packet_ring *ring=socket->ring;
        for(uint32_t i=0;i<ring->frame_count;i++) {
            uint32_t *status=(uint32_t *)(void *)ring_pointer(ring,ring_frame_offset(ring,i));
            if(__atomic_load_n(status,__ATOMIC_ACQUIRE)&PACKET_STATUS_USER) {
                ready|=PACKET_POLLIN;break;
            }
        }
    }
    return ready;
}
static const struct nas_file_ops packet_ops={
    .release=packet_release,.poll_description=packet_poll
};
const struct nas_file_ops *nas_packet_socket_ops(void){return &packet_ops;}
