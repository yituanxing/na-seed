#include "readonly.h"
#include "../../errno.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NVM Express 1.x: administration and NVM READ on one known QEMU namespace.
 * The hardware qualification, PCI config writes and UC BAR are platform-owned.
 * No GPL third-party source is copied into this small acceptance core. */
#define NVME_CAP 0x00U
#define NVME_CC 0x14U
#define NVME_CSTS 0x1cU
#define NVME_AQA 0x24U
#define NVME_ASQ 0x28U
#define NVME_ACQ 0x30U
#define NVME_DOORBELL_BASE 0x1000U
#define NVME_CC_EN 1U
#define NVME_CSTS_RDY 1U
#define NVME_CSTS_CFS 2U
#define NVME_OP_CREATE_IO_SQ 0x01U
#define NVME_OP_CREATE_IO_CQ 0x05U
#define NVME_OP_IDENTIFY 0x06U
#define NVME_OP_NVM_READ 0x02U
#define NVME_POLL_LIMIT 1000000U

static void zero_bytes(void *destination, uint32_t count) {
    uint8_t *p = destination;
    for (uint32_t i=0;i<count;i++)p[i]=0;
}
static void copy_bytes(void *destination, const void *source, uint32_t count) {
    uint8_t *d=destination;
    const uint8_t *s=source;
    for(uint32_t i=0;i<count;i++)d[i]=s[i];
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p+4U)<<32);
}
static void put32(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
    p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24);
}
static int io_read(struct nas_nvme_ro_session *s,uint32_t off,uint32_t *val) {
    if(!s || !s->io || !s->io->read32 || !val || (off&3U) ||
       off>s->io->mmio_bytes || s->io->mmio_bytes-off<4U)return -EINVAL;
    return s->io->read32(s->io->context,off,val);
}
static int io_write(struct nas_nvme_ro_session *s,uint32_t off,uint32_t val) {
    if(!s || !s->io || !s->io->write32 || (off&3U) ||
       off>s->io->mmio_bytes || s->io->mmio_bytes-off<4U)return -EINVAL;
    return s->io->write32(s->io->context,off,val);
}
static int ready_wait(struct nas_nvme_ro_session *s,bool wanted) {
    for(unsigned i=0;i<NVME_POLL_LIMIT;i++) {
        uint32_t status=0;
        int rc=io_read(s,NVME_CSTS,&status);
        if(rc)return rc;
        if(status&NVME_CSTS_CFS)return -EIO;
        if(((status&NVME_CSTS_RDY)!=0U)==wanted)return 0;
    }
    return -ETIMEDOUT;
}
static int addr_write(struct nas_nvme_ro_session *s,uint32_t reg,uint64_t addr) {
    int rc=io_write(s,reg,(uint32_t)addr);
    if(!rc)rc=io_write(s,reg+4U,(uint32_t)(addr>>32));
    return rc;
}
static uint32_t db_off(const struct nas_nvme_ro_session *s,
                       const struct nas_nvme_ro_queue *q,bool cq) {
    uint32_t register_index=(uint32_t)q->qid*2U+(cq?1U:0U);
    return NVME_DOORBELL_BASE+(register_index*4U<<s->doorbell_stride);
}

static int queue_submit(struct nas_nvme_ro_session *s,
                        struct nas_nvme_ro_queue *q,
                        uint8_t opcode,uint32_t nsid,uint64_t prp,
                        uint32_t cdw10,uint32_t cdw11,uint32_t cdw12) {
    if(!q->sq.cpu || !q->cq.cpu || !s->may_dma || !q->cid_next)
        return -EINVAL;
    uint16_t cid=q->cid_next++;
    if(!q->cid_next)q->cid_next=1U;
    uint8_t *cmd=(uint8_t *)q->sq.cpu+(uint32_t)q->sq_tail*64U;
    zero_bytes(cmd,64U);
    put32(cmd,(uint32_t)opcode|((uint32_t)cid<<16));
    put32(cmd+4U,nsid);
    put32(cmd+24U,(uint32_t)prp);
    put32(cmd+28U,(uint32_t)(prp>>32));
    put32(cmd+40U,cdw10);
    put32(cmd+44U,cdw11);
    put32(cmd+48U,cdw12);
    int rc=nas_dma_sync(s->dma,&q->sq,(uint32_t)q->sq_tail*64U,64U,
                        NAS_DMA_TO_DEVICE);
    if(rc)return rc;
    q->sq_tail=(uint8_t)((q->sq_tail+1U)%NAS_NVME_RO_QUEUE_DEPTH);
    rc=io_write(s,db_off(s,q,false),q->sq_tail);
    if(rc)return rc;
    for(unsigned i=0;i<NVME_POLL_LIMIT;i++) {
        uint32_t byte_off=(uint32_t)q->cq_head*16U;
        rc=nas_dma_sync(s->dma,&q->cq,byte_off,16U,NAS_DMA_FROM_DEVICE);
        if(rc)return rc;
        const uint8_t *c=(const uint8_t *)q->cq.cpu+byte_off;
        uint32_t dw3=le32(c+12U);
        if(((dw3>>16)&1U)!=q->cq_phase)continue;
        uint32_t dw2=le32(c+8U);
        if((dw3&0xffffU)!=cid || (dw2>>16)!=q->qid || (dw3>>17)!=0U)
            return -EIO;
        q->cq_head=(uint8_t)((q->cq_head+1U)%NAS_NVME_RO_QUEUE_DEPTH);
        if(!q->cq_head)q->cq_phase^=1U;
        return io_write(s,db_off(s,q,true),q->cq_head);
    }
    return -ETIMEDOUT;
}

int nas_nvme_ro_close(struct nas_nvme_ro_session *s) {
    if(!s || !s->io || !s->dma)return -EINVAL;
    if(s->may_dma) {
        uint32_t cc=0;
        int rc=io_read(s,NVME_CC,&cc);
        if(!rc)rc=io_write(s,NVME_CC,cc&~NVME_CC_EN);
        if(!rc)rc=ready_wait(s,false);
        if(rc)return rc; /* DMA pages stay PINNED on an unsafe stop. */
        s->may_dma=false;
    }
    struct nas_dma_page *pages[]={&s->bounce,&s->data.cq,&s->data.sq,
                                  &s->admin.cq,&s->admin.sq};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        if(pages[i]->cpu) {
            int rc=nas_dma_free_page(s->dma,pages[i]);
            if(rc)return rc;
        }
    }
    *s=(struct nas_nvme_ro_session){0};
    return 0;
}

int nas_nvme_ro_open(const struct nas_nvme_io *io,
                     const struct nas_dma_provider *dma,
                     struct nas_nvme_ro_session *s) {
    if(!io || !io->read32 || !io->write32 || !dma || !s ||
       !dma->alloc_page || !dma->release_page ||
       (!dma->coherent && !dma->sync) ||
       io->mmio_bytes<NVME_DOORBELL_BASE+16U)return -EINVAL;
    if(s->io || s->admin.sq.cpu || s->admin.cq.cpu || s->data.sq.cpu ||
       s->data.cq.cpu || s->bounce.cpu)return -EBUSY;
    *s=(struct nas_nvme_ro_session){.io=io,.dma=dma};
    uint32_t cap_lo=0,cap_hi=0,cc=0;
    int rc=io_read(s,NVME_CAP,&cap_lo);
    if(!rc)rc=io_read(s,NVME_CAP+4U,&cap_hi);
    if(!rc)rc=io_read(s,NVME_CC,&cc);
    if(rc)goto failed;
    uint64_t cap=(uint64_t)cap_lo|((uint64_t)cap_hi<<32);
    if((cap&0xffffU)<NAS_NVME_RO_QUEUE_DEPTH-1U ||
       !(cap&(UINT64_C(1)<<37)) || ((cap>>48)&15U)!=0U) {
        rc=-EOPNOTSUPP;goto failed;
    }
    s->doorbell_stride=(uint8_t)((cap>>32)&15U);
    if(db_off(s,&(struct nas_nvme_ro_queue){.qid=1U},true) >
       io->mmio_bytes-4U) {rc=-EOPNOTSUPP;goto failed;}
    if(cc&NVME_CC_EN) {
        rc=io_write(s,NVME_CC,cc&~NVME_CC_EN);
        if(rc)goto failed;
    }
    rc=ready_wait(s,false);
    if(rc)goto failed;
    struct nas_dma_page *pages[]={&s->admin.sq,&s->admin.cq,
                                  &s->data.sq,&s->data.cq,&s->bounce};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        rc=nas_dma_alloc_page(dma,pages[i]);
        if(rc)goto failed;
        if(pages[i]->bus_address&4095U) {rc=-EIO;goto failed;}
        zero_bytes(pages[i]->cpu,NAS_DMA_PAGE_BYTES);
    }
    s->admin.cid_next=1U;
    s->admin.cq_phase=1U;
    s->admin.qid=0U;
    s->data.cid_next=1U;
    s->data.cq_phase=1U;
    s->data.qid=1U;
    rc=io_write(s,NVME_AQA,(NAS_NVME_RO_QUEUE_DEPTH-1U)|
       ((NAS_NVME_RO_QUEUE_DEPTH-1U)<<16));
    if(!rc)rc=addr_write(s,NVME_ASQ,s->admin.sq.bus_address);
    if(!rc)rc=addr_write(s,NVME_ACQ,s->admin.cq.bus_address);
    if(rc)goto failed;
    s->may_dma=true; /* CC write could succeed despite callback failure. */
    rc=io_write(s,NVME_CC,NVME_CC_EN|(6U<<16)|(4U<<20));
    if(rc)goto failed;
    rc=ready_wait(s,true);
    if(rc)goto failed;

    /* Identify Namespace 1: NVMe admin opcode 06, CNS 0, PRP1=4K page. */
    rc=queue_submit(s,&s->admin,NVME_OP_IDENTIFY,1U,
                    s->bounce.bus_address,0U,0U,0U);
    if(rc)goto failed;
    rc=nas_dma_sync(dma,&s->bounce,0U,NAS_DMA_PAGE_BYTES,
                    NAS_DMA_FROM_DEVICE);
    if(rc)goto failed;
    const uint8_t *ns=s->bounce.cpu;
    s->sectors=le64(ns);
    const uint64_t capacity=le64(ns+8U);
    uint8_t flbas=(uint8_t)(ns[26U]&0xfU);
    uint8_t nlbaf=ns[25U];
    if(!s->sectors || !capacity || capacity>s->sectors ||
       flbas>nlbaf || flbas>=16U) {
        rc=-ENODEV;goto failed;
    }
    const uint8_t *lbaf=ns+128U+(uint32_t)flbas*4U;
    /* H4 is one-sector PRP, no metadata/extended-LBA handling. */
    if((ns[26U]&0x70U) || lbaf[0] || lbaf[1] ||
       (lbaf[2]!=9U && lbaf[2]!=12U)) {
        rc=-EOPNOTSUPP;goto failed;
    }
    uint8_t lbads=lbaf[2];
    s->sector_bytes=UINT32_C(1)<<lbads;

    /* Create I/O Completion Queue 1 (interrupts disabled, physically
     * contiguous), followed by I/O Submission Queue 1 bound to that CQ. */
    const uint32_t qid_and_size=1U|((NAS_NVME_RO_QUEUE_DEPTH-1U)<<16);
    rc=queue_submit(s,&s->admin,NVME_OP_CREATE_IO_CQ,0U,
                    s->data.cq.bus_address,qid_and_size,1U,0U);
    if(rc)goto failed;
    rc=queue_submit(s,&s->admin,NVME_OP_CREATE_IO_SQ,0U,
                    s->data.sq.bus_address,qid_and_size,(1U<<16)|1U,0U);
    if(rc)goto failed;
    s->ready=true;
    return 0;
failed:
    /* Every failed open follows one cleanup path. Before controller enable,
     * cleanup only owns local pages and no DMA can reference them. Once CC
     * may have taken effect, close must prove RDY=0 first, or preserve all
     * DMA descriptors so a later quiesce can retry safely. A clean failure
     * leaves a reusable zeroed session rather than a stale busy handle. */
    (void)nas_nvme_ro_close(s);
    return rc;
}

int nas_nvme_ro_read(struct nas_nvme_ro_session *s,uint64_t lba,
                     uint32_t count,void *dst) {
    if(!s || !s->ready || !dst || count!=1U || lba>=s->sectors ||
       (s->sector_bytes!=512U && s->sector_bytes!=4096U))return -EINVAL;
    zero_bytes(s->bounce.cpu,NAS_DMA_PAGE_BYTES);
    int rc=nas_dma_sync(s->dma,&s->bounce,0U,s->sector_bytes,
                        NAS_DMA_FROM_DEVICE);
    if(rc){s->ready=false;return rc;}
    rc=queue_submit(s,&s->data,NVME_OP_NVM_READ,1U,
                    s->bounce.bus_address,(uint32_t)lba,
                    (uint32_t)(lba>>32),0U /* NLB zero-based: one LBA */);
    if(rc){s->ready=false;return rc;}
    rc=nas_dma_sync(s->dma,&s->bounce,0U,s->sector_bytes,
                    NAS_DMA_FROM_DEVICE);
    if(rc){s->ready=false;return rc;}
    copy_bytes(dst,s->bounce.cpu,s->sector_bytes);
    return 0;
}
