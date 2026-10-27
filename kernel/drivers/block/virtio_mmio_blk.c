/* VirtIO v1.2 modern MMIO split queue -> NaSeed BlockDevice.
 * One synchronous request/queue, polled completion (IRQ wiring is future).
 * All queue/sector logic is ISA-agnostic; platform discovers and maps MMIO.
 *
 * DMA is ONLY from the frame allocator: physical addresses in virtqueue,
 * kernel HHDM aliases for CPU access. Never cast device MMIO to HHDM.
 * The command and status, payload, and ring each own separate 4 KiB frames.
 */
#include "virtio_mmio_blk.h"
#include "../port/block.h"
#include <virtio/virtio_ring.h>
#include "../../mm/frame.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_DISKS 4U
#define QUEUE_SIZE 8U
#define MAX_TRANSFER 4096U
#define R_MAGIC        0x000U
#define R_VERSION      0x004U
#define R_DEVICE_ID    0x008U
#define R_FEATURES     0x010U
#define R_FEATURE_SEL  0x014U
#define R_DRIVER_FEAT  0x020U
#define R_DRIVER_SEL   0x024U
#define R_QUEUE_SEL    0x030U
#define R_QUEUE_MAX    0x034U
#define R_QUEUE_NUM    0x038U
#define R_QUEUE_READY  0x044U
#define R_QUEUE_NOTIFY 0x050U
#define R_STATUS       0x070U
#define R_DESC_LO      0x080U
#define R_DESC_HI      0x084U
#define R_DRIVER_LO    0x090U
#define R_DRIVER_HI    0x094U
#define R_DEVICE_LO    0x0a0U
#define R_DEVICE_HI    0x0a4U
#define R_CAPACITY_LO  0x100U
#define R_CAPACITY_HI  0x104U
#define MAGIC          0x74726976U
#define VERSION        2U
#define DEVICE_BLK     2U
#define STATUS_ACK     1U
#define STATUS_DRIVER  2U
#define STATUS_READY   4U
#define STATUS_FEAT_OK 8U
#define STATUS_FAILED  128U
#define FEAT_RO        (1U<<5)
#define FEAT_FLUSH     (1U<<9)
#define FEAT_VERSION1  1U
#define REQ_IN         0U
#define REQ_OUT        1U
#define REQ_FLUSH      4U
#define MMIO_READ(d,off) ((d)->regs[(off)/4U])
#define MMIO_WRITE(d,off,val) ((d)->regs[(off)/4U]=(uint32_t)(val))

struct virtio_header {uint32_t type,reserved;uint64_t sector;};
struct virt_blk {
    struct nas_port_block port;
    struct vring vr;
    volatile uint32_t *regs;
    uint64_t queue_pa,request_pa,payload_pa;
    uint16_t used_index;
    bool failed;
};
static struct virt_blk disks[MAX_DISKS];
static const char *const names[MAX_DISKS]={
    "virtio0","virtio1","virtio2","virtio3"
};
static unsigned disk_count;
static void barrier(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
static void *cpu_alias(uint64_t pa) {
    return (void *)(uintptr_t)(nas_hhdm_offset()+pa);
}
static void release_pages(struct virt_blk *d) {
    if(d->payload_pa){(void)nas_frame_put(d->payload_pa);d->payload_pa=0;}
    if(d->request_pa){(void)nas_frame_put(d->request_pa);d->request_pa=0;}
    if(d->queue_pa){(void)nas_frame_put(d->queue_pa);d->queue_pa=0;}
}
static int virtio_submit(struct virt_blk *d,uint32_t type,
                         uint64_t sector,void *data,uint32_t bytes) {
    if(!d || d->failed || !d->queue_pa || bytes>MAX_TRANSFER)
        return -EIO;
    if((type==REQ_FLUSH && bytes) ||
       (type!=REQ_FLUSH && (!bytes || !data)))return -EINVAL;
    /* Actual upstream Unikraft/Linux BSD virtqueue layout; no NaSeed-only
     * duplicated ring structs or magic avail/used address offsets. */
    struct vring_desc *desc=d->vr.desc;
    volatile struct vring_avail *avail=d->vr.avail;
    volatile struct vring_used *used=d->vr.used;
    struct virtio_header *hdr=(struct virtio_header *)cpu_alias(d->request_pa);
    volatile uint8_t *status=(volatile uint8_t *)
                              cpu_alias(d->request_pa+0x20U);
    hdr->type=type;hdr->reserved=0;hdr->sector=sector;
    *status=0xffU;
    if(type==REQ_OUT)memcpy(cpu_alias(d->payload_pa),data,bytes);
    desc[0]=(struct vring_desc){
        .addr=d->request_pa,.len=sizeof(*hdr),
        .flags=VRING_DESC_F_NEXT,.next=type==REQ_FLUSH?2U:1U
    };
    if(type!=REQ_FLUSH)
        desc[1]=(struct vring_desc){
            .addr=d->payload_pa,.len=bytes,
            .flags=VRING_DESC_F_NEXT|(type==REQ_IN?VRING_DESC_F_WRITE:0U),.next=2U
        };
    desc[2]=(struct vring_desc){
        .addr=d->request_pa+0x20U,.len=1,.flags=VRING_DESC_F_WRITE
    };
    /* A single inflight chain is serialized on the kernel CPU. All split
     * queue entries and DMA frames are physical and have stable lifetimes. */
    uint16_t submit_index=avail->idx;
    avail->ring[submit_index%QUEUE_SIZE]=0;
    barrier();
    avail->idx=(uint16_t)(submit_index+1);
    barrier();
    MMIO_WRITE(d,R_QUEUE_NOTIFY,0);
    bool completed=false;
    for(unsigned spin=0;spin<30000000U;spin++) {
        barrier();
        if(used->idx!=(uint16_t)d->used_index){
            completed=true;break;
        }
    }
    if(!completed){
        d->failed=true;
        MMIO_WRITE(d,R_STATUS,MMIO_READ(d,R_STATUS)|STATUS_FAILED);
        return -EIO;
    }
    barrier();
    unsigned item=d->used_index%QUEUE_SIZE;
    uint32_t id=used->ring[item].id;
    d->used_index++;
    if(id!=0 || *status!=0)return *status==2?-EOPNOTSUPP:-EIO;
    if(type==REQ_IN)memcpy(data,cpu_alias(d->payload_pa),bytes);
    return 0;
}
static int64_t blk_read(void *backend,uint64_t sector,
                         uint32_t count,void *dst){
    struct virt_blk *d=(struct virt_blk *)backend;
    if(count>MAX_TRANSFER/512U)return -EINVAL;
    return virtio_submit(d,REQ_IN,sector,dst,count*512U);
}
static int64_t blk_write(void *backend,uint64_t sector,
                          uint32_t count,const void *src){
    struct virt_blk *d=(struct virt_blk *)backend;
    if(count>MAX_TRANSFER/512U)return -EINVAL;
    return virtio_submit(d,REQ_OUT,sector,(void *)src,count*512U);
}
static int blk_flush(void *backend){
    return virtio_submit((struct virt_blk *)backend,REQ_FLUSH,0,0,0);
}
static const struct nas_port_block_ops disk_ops={
    .read=blk_read,.write=blk_write,.flush=blk_flush
};
static int configure(struct virt_blk *d) {
    MMIO_WRITE(d,R_STATUS,0);
    MMIO_WRITE(d,R_STATUS,STATUS_ACK|STATUS_DRIVER);
    MMIO_WRITE(d,R_FEATURE_SEL,0);
    uint32_t low=MMIO_READ(d,R_FEATURES);
    MMIO_WRITE(d,R_FEATURE_SEL,1);
    uint32_t high=MMIO_READ(d,R_FEATURES);
    if(!(high&FEAT_VERSION1))return -EOPNOTSUPP;
    /* Explicitly negotiate only modern transport and FLUSH; no packed ring,
     * indirect descriptors, ACCESS_PLATFORM or unsupported blk extensions. */
    MMIO_WRITE(d,R_DRIVER_SEL,0);
    MMIO_WRITE(d,R_DRIVER_FEAT,low&FEAT_FLUSH);
    MMIO_WRITE(d,R_DRIVER_SEL,1);
    MMIO_WRITE(d,R_DRIVER_FEAT,FEAT_VERSION1);
    MMIO_WRITE(d,R_STATUS,STATUS_ACK|STATUS_DRIVER|STATUS_FEAT_OK);
    if(!(MMIO_READ(d,R_STATUS)&STATUS_FEAT_OK))return -EOPNOTSUPP;
    MMIO_WRITE(d,R_QUEUE_SEL,0);
    if(MMIO_READ(d,R_QUEUE_READY)||MMIO_READ(d,R_QUEUE_MAX)<QUEUE_SIZE)
        return -EOPNOTSUPP;
    d->queue_pa=nas_frame_alloc();
    d->request_pa=nas_frame_alloc();
    d->payload_pa=nas_frame_alloc();
    if(!d->queue_pa||!d->request_pa||!d->payload_pa)return -ENOMEM;
    /* Use upstream vring_init / vring_size for descriptor, driver and used
     * positions. One preallocated DMA frame still holds the whole split ring. */
    if(vring_size(QUEUE_SIZE,256U)>4096U)return -EOVERFLOW;
    void *queue_va=cpu_alias(d->queue_pa);
    memset(queue_va,0,4096U);
    vring_init(&d->vr,QUEUE_SIZE,queue_va,256U);
    uint64_t avail_pa=d->queue_pa+
            (uint64_t)((uintptr_t)d->vr.avail-(uintptr_t)queue_va);
    uint64_t used_pa=d->queue_pa+
            (uint64_t)((uintptr_t)d->vr.used-(uintptr_t)queue_va);
    MMIO_WRITE(d,R_QUEUE_NUM,QUEUE_SIZE);
    MMIO_WRITE(d,R_DESC_LO,(uint32_t)d->queue_pa);
    MMIO_WRITE(d,R_DESC_HI,(uint32_t)(d->queue_pa>>32));
    MMIO_WRITE(d,R_DRIVER_LO,(uint32_t)avail_pa);
    MMIO_WRITE(d,R_DRIVER_HI,(uint32_t)(avail_pa>>32));
    MMIO_WRITE(d,R_DEVICE_LO,(uint32_t)used_pa);
    MMIO_WRITE(d,R_DEVICE_HI,(uint32_t)(used_pa>>32));
    barrier();
    MMIO_WRITE(d,R_QUEUE_READY,1);
    uint64_t sectors=(uint64_t)MMIO_READ(d,R_CAPACITY_LO)|
                      ((uint64_t)MMIO_READ(d,R_CAPACITY_HI)<<32);
    if(!sectors)return -ENODEV;
    bool writable=!(low&FEAT_RO) && (low&FEAT_FLUSH);
    MMIO_WRITE(d,R_STATUS,STATUS_ACK|STATUS_DRIVER|STATUS_FEAT_OK|STATUS_READY);
    return nas_port_block_attach(&d->port,names[disk_count],d,sectors,512U,
                                 MAX_TRANSFER/512U,!writable,
                                 NAS_PORT_BLOCK_ZERO,&disk_ops);
}
int nas_virtio_mmio_blk_attach(volatile uint32_t *regs) {
    if(!regs || regs[R_MAGIC/4]!=MAGIC ||
       regs[R_VERSION/4]!=VERSION || regs[R_DEVICE_ID/4]!=DEVICE_BLK)
        return 0;
    if(disk_count>=MAX_DISKS)return -ENOSPC;
    struct virt_blk *d=&disks[disk_count];
    d->regs=regs;d->used_index=0;d->failed=false;
    int rc=configure(d);
    if(rc){
        MMIO_WRITE(d,R_STATUS,STATUS_FAILED);
        release_pages(d);
        d->regs=NULL;
        return rc;
    }
    disk_count++;
    return 1;
}
unsigned nas_virtio_mmio_blk_count(void){return disk_count;}
