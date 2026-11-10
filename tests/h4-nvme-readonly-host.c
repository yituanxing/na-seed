#include "../kernel/drivers/nvme/readonly.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *frames[16];
static unsigned alloc_count,free_count;
static bool active_dma,invalid_free;
uint64_t nas_frame_alloc(void) {
    if(alloc_count==16U)return 0;
    void *page=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    if(!page)return 0;
    memset(page,0,NAS_DMA_PAGE_BYTES);
    frames[alloc_count++]=page;
    return (uint64_t)(uintptr_t)page;
}
uint64_t nas_hhdm_offset(void) {return 0;}
bool nas_frame_put(uint64_t pa) {
    if(active_dma){invalid_free=true;return false;}
    for(unsigned i=0;i<alloc_count;i++) {
        if(frames[i]==(void *)(uintptr_t)pa) {
            free(frames[i]);frames[i]=NULL;free_count++;return true;
        }
    }
    return false;
}

static void put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8U*i));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t reg64(const uint32_t *reg,uint32_t offset) {
    return (uint64_t)reg[offset/4U]|((uint64_t)reg[offset/4U+1U]<<32);
}

struct fake_nvme {
    uint32_t reg[0x2000/4];
    uint64_t io_sq,io_cq;
    uint8_t *admin_cq, *read_cq;
    uint8_t admin_sq_head,admin_cq_tail,io_sq_head,io_cq_tail;
    uint8_t admin_phase,io_phase;
    unsigned identify_count,create_cq_count,create_sq_count,read_count;
    bool no_completion,bad_namespace,fail_disable,io_failure,bad_opcode;
    bool four_k,metadata,bad_capacity;
};

static uint64_t cmd_prp(const uint8_t *sq) {
    return (uint64_t)le32(sq+24U)|((uint64_t)le32(sq+28U)<<32);
}
static int fake_read(void *context,uint32_t off,uint32_t *val) {
    struct fake_nvme *f=context;
    if(!val || (off&3U) || off>=sizeof(f->reg))return -EIO;
    *val=f->reg[off/4U];return 0;
}
static void complete(struct fake_nvme *f,bool admin,const uint8_t *sq,
                     uint16_t status) {
    uint8_t *cq=admin ? (uint8_t *)(uintptr_t)reg64(f->reg,0x30U) :
                         (uint8_t *)(uintptr_t)f->io_cq;
    uint8_t tail=admin ? f->admin_cq_tail : f->io_cq_tail;
    uint8_t phase=admin ? f->admin_phase : f->io_phase;
    put32(cq+(uint32_t)tail*16U+8U,
          ((uint32_t)(admin?0U:1U)<<16)|((uint32_t)tail+1U));
    uint32_t cid=(le32(sq)>>16)&0xffffU;
    put32(cq+(uint32_t)tail*16U+12U,cid|((uint32_t)status<<17)|
          ((uint32_t)phase<<16));
    if(admin) {
        f->admin_cq_tail=(uint8_t)((tail+1U)%NAS_NVME_RO_QUEUE_DEPTH);
        if(!f->admin_cq_tail)f->admin_phase^=1U;
    } else {
        f->io_cq_tail=(uint8_t)((tail+1U)%NAS_NVME_RO_QUEUE_DEPTH);
        if(!f->io_cq_tail)f->io_phase^=1U;
    }
}
static void admin_command(struct fake_nvme *f) {
    if(f->no_completion)return;
    uint8_t *sq=(uint8_t *)(uintptr_t)reg64(f->reg,0x28U)+
                (uint32_t)f->admin_sq_head*64U;
    f->admin_sq_head=(uint8_t)((f->admin_sq_head+1U)%NAS_NVME_RO_QUEUE_DEPTH);
    uint32_t op=le32(sq)&0xffU;
    uint32_t nsid=le32(sq+4U),cdw10=le32(sq+40U);
    if(op==6U && nsid==1U && cdw10==0U) {
        ++f->identify_count;
        uint8_t *data=(uint8_t *)(uintptr_t)cmd_prp(sq);
        if(!f->bad_namespace) {
            put32(data,128U); /* NSZE: 128 sectors. */
            put32(data+8U,f->bad_capacity?129U:128U);
            data[25U]=0; /* NLBAF */
            data[26U]=(uint8_t)(f->metadata?0x10U:0U);
            data[130U]=(uint8_t)(f->four_k?12U:9U);
        }
    } else if(op==5U && nsid==0U && (cdw10&0xffffU)==1U &&
              (le32(sq+44U)&1U)) {
        ++f->create_cq_count;
        f->io_cq=cmd_prp(sq);
    } else if(op==1U && nsid==0U && (cdw10&0xffffU)==1U &&
              ((le32(sq+44U)>>16)==1U)) {
        ++f->create_sq_count;
        f->io_sq=cmd_prp(sq);
    } else f->bad_opcode=true;
    complete(f,true,sq,0U);
}
static void read_command(struct fake_nvme *f) {
    uint8_t *sq=(uint8_t *)(uintptr_t)f->io_sq+
                (uint32_t)f->io_sq_head*64U;
    f->io_sq_head=(uint8_t)((f->io_sq_head+1U)%NAS_NVME_RO_QUEUE_DEPTH);
    uint32_t op=le32(sq)&0xffU,nsid=le32(sq+4U);
    uint64_t lba=(uint64_t)le32(sq+40U)|
                 ((uint64_t)le32(sq+44U)<<32);
    if(op!=2U || nsid!=1U || lba>127U || le32(sq+48U)!=0U) {
        f->bad_opcode=true;
        complete(f,false,sq,0x02U);
        return;
    }
    ++f->read_count;
    uint8_t *data=(uint8_t *)(uintptr_t)cmd_prp(sq);
    memset(data,0,NAS_DMA_PAGE_BYTES);
    for(unsigned i=0;i<(f->four_k?4096U:512U);i++)
        data[i]=(uint8_t)((i+17U*lba)&255U);
    complete(f,false,sq,f->io_failure?0x02U:0U);
}
static int fake_write(void *context,uint32_t off,uint32_t val) {
    struct fake_nvme *f=context;
    if((off&3U)||off>=sizeof(f->reg))return -EIO;
    f->reg[off/4U]=val;
    if(off==0x14U) {
        if(val&1U)f->reg[0x1cU/4U]=1U;
        else if(!f->fail_disable)f->reg[0x1cU/4U]=0U;
        active_dma=(f->reg[0x1cU/4U]&1U)!=0U;
    }
    if(off==0x1000U && val>0U)admin_command(f);
    if(off==0x1008U)read_command(f);
    return 0;
}
static void reset(struct fake_nvme *f) {
    for(unsigned i=0;i<alloc_count;i++) {
        if(frames[i])free(frames[i]);
        frames[i]=NULL;
    }
    alloc_count=free_count=0U;
    active_dma=invalid_free=false;
    memset(f,0,sizeof(*f));
    f->reg[0]=63U;
    f->reg[1]=1U<<5; /* CAP.CSS.NVM. */
    f->admin_phase=f->io_phase=1U;
}
static int open_session(struct fake_nvme *f,struct nas_nvme_ro_session *s,
                        struct nas_nvme_io *io) {
    *io=(struct nas_nvme_io){.context=f,.mmio_bytes=sizeof(f->reg),
                            .read32=fake_read,.write32=fake_write};
    return nas_nvme_ro_open(io,&nas_dma_qemu_coherent,s);
}
#define CHECK(expr) do {if(!(expr)){ \
    fprintf(stderr,"H4 NVMe FAIL at line %u: %s\n",__LINE__,#expr); \
    return 1; }}while(0)

int main(void) {
    struct fake_nvme f;
    struct nas_nvme_ro_session session={0};
    struct nas_nvme_io io={0};
    uint8_t sector[4096]={0};
    reset(&f);
    CHECK(open_session(&f,&session,&io)==0);
    CHECK(session.sectors==128U && session.sector_bytes==512U);
    CHECK(f.identify_count==1 && f.create_cq_count==1 &&
          f.create_sq_count==1 && !f.bad_opcode);
    CHECK(nas_nvme_ro_read(&session,0U,1U,sector)==0);
    for(unsigned i=0;i<512;i++)CHECK(sector[i]==(uint8_t)i);
    CHECK(nas_nvme_ro_read(&session,1U,1U,sector)==0);
    for(unsigned i=0;i<512;i++)CHECK(sector[i]==(uint8_t)(i+17U));
    /* Four-deep queue wrap must toggle CQ phase without stale success. */
    for(unsigned n=2;n<7;n++) {
        CHECK(nas_nvme_ro_read(&session,n,1U,sector)==0);
        for(unsigned i=0;i<512;i++)CHECK(sector[i]==(uint8_t)(i+17U*n));
    }
    CHECK(nas_nvme_ro_read(&session,128U,1U,sector)==-EINVAL);
    CHECK(nas_nvme_ro_read(&session,0U,2U,sector)==-EINVAL);
    CHECK(f.read_count==7U && !f.bad_opcode);
    CHECK(nas_nvme_ro_close(&session)==0);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    f.four_k=true;
    CHECK(open_session(&f,&session,&io)==0);
    CHECK(session.sector_bytes==4096U);
    CHECK(nas_nvme_ro_read(&session,1U,1U,sector)==0);
    for(unsigned i=0;i<4096U;i++)CHECK(sector[i]==(uint8_t)(i+17U));
    CHECK(nas_nvme_ro_close(&session)==0);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    f.metadata=true;
    CHECK(open_session(&f,&session,&io)==-EOPNOTSUPP);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    f.bad_capacity=true;
    CHECK(open_session(&f,&session,&io)==-ENODEV);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    f.bad_namespace=true;
    CHECK(open_session(&f,&session,&io)==-ENODEV);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    f.no_completion=true;
    CHECK(open_session(&f,&session,&io)==-ETIMEDOUT);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    f.fail_disable=true;f.bad_namespace=true;
    CHECK(open_session(&f,&session,&io)==-ENODEV);
    CHECK(alloc_count==5U && free_count==0U && !invalid_free);

    reset(&f);session=(struct nas_nvme_ro_session){0};
    CHECK(open_session(&f,&session,&io)==0);
    f.io_failure=true;
    CHECK(nas_nvme_ro_read(&session,0U,1U,sector)==-EIO);
    CHECK(nas_nvme_ro_close(&session)==0);
    CHECK(alloc_count==5U && free_count==5U && !invalid_free);

    puts("[H4 NVMe READ-ONLY HOST PASS] namespace geometry, IO queue/LBA 0-1, errors and DMA teardown");
    return 0;
}
