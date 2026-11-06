/* Real packet socket/TPACKET_V2 host oracle. No fabricated kernel packets:
 * NIC RX/TX enters through the actual netdev driver boundary, with PMM
 * frames and a live VM device pin whose lifetime outlasts the socket FD. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/errno.h"
#include "../kernel/fd.h"
#include "../kernel/fs/file_ops.h"
#include "../kernel/fs/packet_socket.h"
#include "../kernel/drivers/port/net.h"

struct nas_open_file nas_open_files[NAS_OPEN_FILE_LIMIT];
static unsigned frames_live,frames_freed;
void *nas_heap_alloc(size_t size){return malloc(size);}
void *nas_heap_calloc(size_t count,size_t size){return calloc(count,size);}
void *nas_heap_realloc(void *p,size_t size){return realloc(p,size);}
bool nas_heap_free(void *p){free(p);return true;}
uint64_t nas_frame_alloc(void) {
    void *mem=aligned_alloc(4096,4096);
    assert(mem);
    memset(mem,0,4096);
    frames_live++;
    return (uint64_t)(uintptr_t)mem;
}
bool nas_frame_put(uint64_t pa) {
    assert(pa && frames_live);
    free((void *)(uintptr_t)pa);
    frames_live--;frames_freed++;
    return true;
}
uint64_t nas_hhdm_offset(void){return 0;}
bool nas_time_realtime_ns(uint64_t *value) {
    *value=1700000000123456789ULL;
    return true;
}
int nas_lwip_guest_ifname(unsigned index,char name[16]) {
    if(index!=2)return -ENODEV;
    strcpy(name,"eth0");return 0;
}
static unsigned tx_count,rx_count;
static uint8_t original[74];
static int transmit(void *ctx,const uint8_t *data,uint32_t length) {
    assert(ctx==&tx_count && data && length==sizeof(original));
    tx_count++;return 0;
}
static int poll_driver(void *ctx,uint32_t budget,nas_netdev_rx deliver,void *consumer) {
    assert(ctx==&tx_count && budget==4);
    return deliver(consumer,original,sizeof(original))? -EIO:1;
}
static int consume(void *ctx,const uint8_t *frame,uint32_t length) {
    assert(ctx==&rx_count && frame && length==sizeof(original));
    rx_count++;return 0;
}
static bool link(void *ctx){return ctx==&tx_count;}
static const struct nas_netdev_ops ops={
    .transmit=transmit,.poll=poll_driver,.link_up=link};
static struct nas_netdev nic={
    .name="eth0",.mac={0x52,0x54,0,0x12,0x34,0x56},
    .mtu=1500,.backend=&tx_count,.ops=&ops};
static uint8_t *frame_ptr(struct nas_vm_device *dev,uint32_t off) {
    uint64_t frame=0;
    assert(dev->page(dev->context,off,&frame)==0);
    return (uint8_t *)(uintptr_t)(frame+off%4096U);
}
static uint32_t u32(const uint8_t *p){uint32_t value;memcpy(&value,p,4);return value;}
static uint16_t u16(const uint8_t *p){uint16_t value;memcpy(&value,p,2);return value;}

int main(void) {
    for(unsigned i=0;i<sizeof(original);i++)original[i]=(uint8_t)(i+3U);
    original[12]=8;original[13]=0; /* real IPv4 EtherType */
    assert(nas_netdev_register(&nic)==0);
    nas_open_files[12].refcount=1;
    assert(nas_packet_socket_open(12,3,0)==0);
    assert(nas_packet_socket_is_slot(12));
    /* There is no attached kernel BPF filter. Linux SO_DETACH_FILTER must
     * report ENOENT so unmodified libpcap can use userspace filtering.
     * Claiming a successful detach would conceal unsupported filtering. */
    assert(nas_packet_socket_detach_filter(12)==-ENOENT);
    assert(nas_packet_socket_bind(12,2,0)==0);
    assert(nic.refs==1);
    assert(nas_packet_socket_bind(12,9,3)==-ENODEV);
    assert(nic.refs==1); /* failed rebind cannot leak old NIC pin */
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1 && rx_count==1);
    struct nas_packet_ring_request request={4096,2,2048,4};
    unsigned setting=2;
    assert(nas_packet_socket_version(12,&setting,true)==-EINVAL);
    setting=1;
    assert(nas_packet_socket_version(12,&setting,true)==0);
    setting=4;
    assert(nas_packet_socket_reserve(12,&setting,true)==0);
    struct nas_packet_ring_request bad={4096,2,2048,3};
    assert(nas_packet_socket_ring(12,&bad)==-EINVAL);
    bad=(struct nas_packet_ring_request){4096,0,2048,0};
    assert(nas_packet_socket_ring(12,&bad)==-EINVAL);
    bad=(struct nas_packet_ring_request){4096,3000,2048,6000};
    assert(nas_packet_socket_ring(12,&bad)==-EINVAL);
    bad=(struct nas_packet_ring_request){4096,2,144,55};
    assert(nas_packet_socket_ring(12,&bad)==-EINVAL);
    assert(frames_live==0); /* geometry errors allocate nothing */
    assert(nas_packet_socket_ring(12,&request)==0);
    assert(nas_packet_socket_ring(12,&request)==-EBUSY);
    setting=0;
    assert(nas_packet_socket_version(12,&setting,true)==-EINVAL);
    assert(nas_packet_socket_reserve(12,&setting,true)==-EBUSY);
    assert(frames_live==2);
    struct nas_vm_device *dev=NULL;
    assert(nas_packet_socket_mmap_pin(12,&dev)==0);
    assert(dev && dev->extent_bytes==8192);
    assert(nas_packet_socket_bind(12,2,3)==0 && nic.refs==1);
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1 && rx_count==2);
    uint8_t *f0=frame_ptr(dev,0);
    assert(__atomic_load_n((uint32_t *)f0,__ATOMIC_ACQUIRE)==1);
    assert(u32(f0+4)==74 && u32(f0+8)==74);
    assert(u16(f0+12)==70 && u16(f0+14)==84);
    assert(u32(f0+16)==1700000000U && u32(f0+20)==123456789U);
    assert(u16(f0+32)==17 && f0[34]==8 && f0[35]==0);
    assert(u32(f0+36)==2 && u16(f0+40)==1 && f0[43]==6);
    assert(memcmp(f0+70,original,sizeof(original))==0);
    assert(nas_netdev_transmit(&nic,original,sizeof(original))==0);
    uint8_t *f1=frame_ptr(dev,2048);
    assert(__atomic_load_n((uint32_t *)f1,__ATOMIC_ACQUIRE)==1);
    assert(f1[42]==4); /* packet outgoing, not a second RX */
    assert(tx_count==1);
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1);
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1);
    /* All frames now owned by userspace; producer must report real drops. */
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1);
    struct nas_packet_stats stats={0};
    assert(nas_packet_socket_stats(12,&stats)==0);
    assert(stats.packets==5 && stats.drops==1);
    assert(nas_packet_socket_stats(12,&stats)==0);
    assert(!stats.packets && !stats.drops);
    /* The packet consumer release-store reopens one frame to the producer. */
    __atomic_store_n((uint32_t *)f0,0,__ATOMIC_RELEASE);
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1);
    assert(__atomic_load_n((uint32_t *)f0,__ATOMIC_ACQUIRE)==1);
    /* A VMA may outlive the FD and the reused OpenFile slot. */
    const struct nas_file_ops *fileops=nas_packet_socket_ops();
    assert(fileops->poll_description(12,1)&1U);
    fileops->release(12);
    nas_open_files[12].refcount=0;
    assert(!nas_packet_socket_is_slot(12) && nic.refs==0);
    assert(nas_packet_socket_detach_filter(12)==-EBADF);
    assert(frames_live==2 && frame_ptr(dev,0)==f0);
    dev->put(dev->context);
    assert(frames_live==0 && frames_freed==2);
    /* libpcap 1.10.x derives tp_frame_size from snapshot+TPACKET2 headers:
     * it is 16-byte aligned, NOT necessarily page aligned or an exact
     * divisor of its power-of-two block size. Linux's accepted layout has
     * one 262208-byte frame per 524288-byte block and 262080-byte slack.
     * Frame #1 is at block #1, NOT immediately after frame #0. */
    nas_open_files[13].refcount=1;
    assert(nas_packet_socket_open(13,3,0)==0);
    assert(nas_packet_socket_bind(13,2,3)==0);
    setting=1;
    assert(nas_packet_socket_version(13,&setting,true)==0);
    struct nas_packet_ring_request libpcap={524288,4,262208,4};
    assert(nas_packet_socket_ring(13,&libpcap)==0);
    assert(frames_live==512);
    struct nas_vm_device *large_dev=NULL;
    assert(nas_packet_socket_mmap_pin(13,&large_dev)==0);
    assert(large_dev && large_dev->extent_bytes==2097152);
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1);
    assert(nas_netdev_poll(&nic,4,consume,&rx_count)==1);
    uint8_t *large_f0=frame_ptr(large_dev,0);
    uint8_t *large_f1=frame_ptr(large_dev,524288);
    assert(u32(large_f0)==1 && u32(large_f1)==1);
    assert(u32(frame_ptr(large_dev,262208))==0); /* real block slack */
    assert(u32(large_f1+4)==sizeof(original));
    assert(memcmp(large_f1+66,original,sizeof(original))==0);
    assert(fileops->poll_description(13,1)&1U);
    fileops->release(13);
    nas_open_files[13].refcount=0;
    assert(nic.refs==0 && frames_live==512);
    large_dev->put(large_dev->context);
    assert(frames_live==0 && frames_freed==514);
    assert(nas_netdev_unregister(&nic)==0);
    puts("[PACKET SOCKET PASS] real NIC RX/TX, V2 PMM mmap, libpcap block slack, protocol-0 rebind, ring status, drop stats, VMA lifetime");
    return 0;
}
