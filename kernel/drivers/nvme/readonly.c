#include "readonly.h"
#include "controller_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* NVM Express 1.x: administration and NVM READ on one known QEMU namespace.
 * The hardware qualification, PCI config writes and UC BAR are platform-owned.
 * No GPL third-party source is copied into this small acceptance core. */
#define NVME_DOORBELL_BASE 0x1000U
#define NVME_OP_CREATE_IO_SQ 0x01U
#define NVME_OP_CREATE_IO_CQ 0x05U
#define NVME_OP_IDENTIFY 0x06U
#define NVME_OP_NVM_READ 0x02U

static void zero_bytes(void *destination, uint32_t count) {
    uint8_t *p = destination;
    for (uint32_t i=0;i<count;i++)p[i]=0;
}
static void copy_bytes(void *destination, const void *source, uint32_t count) {
    uint8_t *d=destination;
    const uint8_t *s=source;
    for(uint32_t i=0;i<count;i++)d[i]=s[i];
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
    nas_put_le32(cmd,(uint32_t)opcode|((uint32_t)cid<<16));
    nas_put_le32(cmd+4U,nsid);
    nas_put_le32(cmd+24U,(uint32_t)prp);
    nas_put_le32(cmd+28U,(uint32_t)(prp>>32));
    nas_put_le32(cmd+40U,cdw10);
    nas_put_le32(cmd+44U,cdw11);
    nas_put_le32(cmd+48U,cdw12);
    int rc=nas_dma_lease_sync(&s->lease,&q->sq,
                              (uint32_t)q->sq_tail*64U,64U,
                              NAS_DMA_TO_DEVICE);
    if(rc)return rc;
    q->sq_tail=(uint8_t)((q->sq_tail+1U)%NAS_NVME_RO_QUEUE_DEPTH);
    rc=nas_nvme_mmio_write(s->io,db_off(s,q,false),q->sq_tail);
    if(rc)return rc;
    for(unsigned i=0;i<NVME_POLL_LIMIT;i++) {
        uint32_t byte_off=(uint32_t)q->cq_head*16U;
        rc=nas_dma_lease_sync(&s->lease,&q->cq,byte_off,16U,
                              NAS_DMA_FROM_DEVICE);
        if(rc)return rc;
        const uint8_t *c=(const uint8_t *)q->cq.cpu+byte_off;
        uint32_t dw3=nas_get_le32(c+12U);
        if(((dw3>>16)&1U)!=q->cq_phase)continue;
        uint32_t dw2=nas_get_le32(c+8U);
        if((dw3&0xffffU)!=cid || (dw2>>16)!=q->qid || (dw3>>17)!=0U)
            return -EIO;
        q->cq_head=(uint8_t)((q->cq_head+1U)%NAS_NVME_RO_QUEUE_DEPTH);
        if(!q->cq_head)q->cq_phase^=1U;
        return nas_nvme_mmio_write(s->io,db_off(s,q,true),q->cq_head);
    }
    return -ETIMEDOUT;
}

int nas_nvme_ro_close(struct nas_nvme_ro_session *s) {
    if(!s)return -EINVAL;
    if(__atomic_exchange_n(&s->in_io,true,__ATOMIC_ACQUIRE))
        return -EBUSY;
    if(!s->io || !s->dma) {
        __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
        return -EINVAL;
    }
    /* Once stop was attempted, even a timeout may have transitioned CC;
     * no subsequent filesystem read can resubmit commands on this session. */
    s->ready=false;
    int result=0;
    if(s->may_dma) {
        uint32_t cc=0;
        int rc=nas_nvme_mmio_read(s->io,NVME_CC,&cc);
        if(!rc)rc=nas_nvme_mmio_write(s->io,NVME_CC,cc&~NVME_CC_EN);
        if(!rc)rc=nas_nvme_poll_ready(s->io,false);
        if(rc){result=rc;goto failed;} /* PINNED on unsafe stop. */
        rc=nas_dma_lease_disarm_after_quiesce(&s->lease);
        if(rc){result=rc;goto failed;}
        s->may_dma=false;
    }
    struct nas_dma_page *pages[]={&s->bounce,&s->data.cq,&s->data.sq,
                                  &s->admin.cq,&s->admin.sq};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        if(pages[i]->cpu) {
            int rc=nas_dma_lease_free_page(&s->lease,pages[i]);
            if(rc){result=rc;goto failed;}
        }
    }
    int lease_rc=nas_dma_lease_end(&s->lease);
    if(lease_rc){result=lease_rc;goto failed;}
    *s=(struct nas_nvme_ro_session){0};
    return 0;
failed:
    __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
    return result;
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
    int lease_rc=nas_dma_lease_begin(&s->lease,dma);
    if(lease_rc){*s=(struct nas_nvme_ro_session){0};return lease_rc;}
    uint32_t cap_lo=0,cap_hi=0,cc=0;
    int rc=nas_nvme_mmio_read(s->io,NVME_CAP,&cap_lo);
    if(!rc)rc=nas_nvme_mmio_read(s->io,NVME_CAP+4U,&cap_hi);
    if(!rc)rc=nas_nvme_mmio_read(s->io,NVME_CC,&cc);
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
        rc=nas_nvme_mmio_write(s->io,NVME_CC,cc&~NVME_CC_EN);
        if(rc)goto failed;
    }
    rc=nas_nvme_poll_ready(s->io,false);
    if(rc)goto failed;
    struct nas_dma_page *pages[]={&s->admin.sq,&s->admin.cq,
                                  &s->data.sq,&s->data.cq,&s->bounce};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        rc=nas_dma_lease_alloc_page(&s->lease,pages[i]);
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
    rc=nas_nvme_mmio_write(s->io,NVME_AQA,(NAS_NVME_RO_QUEUE_DEPTH-1U)|
       ((NAS_NVME_RO_QUEUE_DEPTH-1U)<<16));
    if(!rc)rc=nas_nvme_write_dma_address(s->io,NVME_ASQ,s->admin.sq.bus_address);
    if(!rc)rc=nas_nvme_write_dma_address(s->io,NVME_ACQ,s->admin.cq.bus_address);
    if(rc)goto failed;
    /* Arm before CC: a callback can report failure after the controller has
     * already observed the write. An armed lease blocks all DMA frees. */
    rc=nas_dma_lease_arm(&s->lease);
    if(rc)goto failed;
    s->may_dma=true;
    rc=nas_nvme_mmio_write(s->io,NVME_CC,NVME_CC_EN|(6U<<16)|(4U<<20));
    if(rc)goto failed;
    rc=nas_nvme_poll_ready(s->io,true);
    if(rc)goto failed;

    /* Identify Namespace 1: NVMe admin opcode 06, CNS 0, PRP1=4K page. */
    rc=queue_submit(s,&s->admin,NVME_OP_IDENTIFY,1U,
                    s->bounce.bus_address,0U,0U,0U);
    if(rc)goto failed;
    rc=nas_dma_lease_sync(&s->lease,&s->bounce,0U,NAS_DMA_PAGE_BYTES,
                          NAS_DMA_FROM_DEVICE);
    if(rc)goto failed;
    const uint8_t *ns=s->bounce.cpu;
    s->sectors=nas_get_le64(ns);
    const uint64_t capacity=nas_get_le64(ns+8U);
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
    if(!s || !dst || !count)return -EINVAL;
    if(__atomic_exchange_n(&s->in_io,true,__ATOMIC_ACQUIRE))
        return -EBUSY; /* one completion queue and one shared bounce page */
    if(!s->ready || !s->may_dma || !s->bounce.cpu ||
       lba>=s->sectors || (uint64_t)count>s->sectors-lba ||
       (s->sector_bytes!=512U && s->sector_bytes!=4096U) ||
       (size_t)count>SIZE_MAX/s->sector_bytes) {
        __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
        return -EINVAL;
    }
    int rc=0;
    /* Filesystems request multi-sector extents. Keep the H4-tested,
     * one-sector/one-PRP1 command invariant: bounded sequential reads into
     * the same leased 4 KiB bounce page and commit only completed sectors
     * to the destination. An error never masquerades as a short success. */
    for(uint32_t i=0;i<count;i++) {
        zero_bytes(s->bounce.cpu,NAS_DMA_PAGE_BYTES);
        rc=nas_dma_lease_sync(&s->lease,&s->bounce,0U,s->sector_bytes,
                              NAS_DMA_FROM_DEVICE);
        if(rc)goto failed;
        uint64_t current=lba+(uint64_t)i;
        rc=queue_submit(s,&s->data,NVME_OP_NVM_READ,1U,
                        s->bounce.bus_address,(uint32_t)current,
                        (uint32_t)(current>>32),0U);
        if(rc)goto failed;
        rc=nas_dma_lease_sync(&s->lease,&s->bounce,0U,s->sector_bytes,
                              NAS_DMA_FROM_DEVICE);
        if(rc)goto failed;
        copy_bytes((uint8_t *)dst+(size_t)i*s->sector_bytes,
                   s->bounce.cpu,s->sector_bytes);
    }
    __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
    return 0;
failed:
    s->ready=false;
    __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
    return rc;
}
