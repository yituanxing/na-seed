/* H9: an actual protocol-level AHCI register/DMA model, independent of QEMU.
 * QEMU serial and disk SHA acceptance must be separate before claiming H9. */
#include "../kernel/drivers/ahci/readonly.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AHCI_PORT 0x100U
#define AHCI_PX_CLB (AHCI_PORT+0x00U)
#define AHCI_PX_FB (AHCI_PORT+0x08U)
#define AHCI_PX_IS (AHCI_PORT+0x10U)
#define AHCI_PX_CMD (AHCI_PORT+0x18U)
#define AHCI_PX_TFD (AHCI_PORT+0x20U)
#define AHCI_PX_SIG (AHCI_PORT+0x24U)
#define AHCI_PX_SSTS (AHCI_PORT+0x28U)
#define AHCI_PX_SERR (AHCI_PORT+0x30U)
#define AHCI_PX_SACT (AHCI_PORT+0x34U)
#define AHCI_PX_CI (AHCI_PORT+0x38U)
#define AHCI_PORT1 0x180U
#define AHCI_PORT1_SIG (AHCI_PORT1+0x24U)
#define AHCI_PORT1_SSTS (AHCI_PORT1+0x28U)
#define AHCI_GHC 0x04U
#define AHCI_PI 0x0cU
#define AHCI_CAP_S64A (UINT32_C(1)<<31)
#define AHCI_CMD_ST (1U<<0)
#define AHCI_CMD_FRE (1U<<4)
#define AHCI_CMD_FR (1U<<14)
#define AHCI_CMD_CR (1U<<15)
#define ATA_IDENTIFY 0xecU
#define ATA_READ_DMA_EXT 0x25U

#define CHECK(expr) do { if(!(expr)) { \
    fprintf(stderr,"[H9 AHCI HOST FAIL] line %u: %s\n",__LINE__,#expr); \
    return 1; \
} } while(0)

static void *frames[16];
static unsigned allocations,frees;
static bool active_dma,invalid_free;
uint64_t nas_frame_alloc(void) {
    if(allocations>=16U)return 0;
    void *p=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    if(!p)return 0;
    memset(p,0,NAS_DMA_PAGE_BYTES);
    frames[allocations++]=p;
    return (uint64_t)(uintptr_t)p;
}
uint64_t nas_hhdm_offset(void) {return 0;}
bool nas_frame_put(uint64_t pa) {
    if(active_dma){invalid_free=true;return false;}
    for(unsigned i=0;i<allocations;i++){
        if(frames[i]==(void *)(uintptr_t)pa){
            free(frames[i]);frames[i]=NULL;frees++;return true;
        }
    }
    return false;
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p)|((uint64_t)get32(p+4U)<<32);
}
static void put16(uint8_t *p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
struct fake_ahci {
    uint32_t reg[0x2000/4];
    unsigned identify_count,read_count,illegal_ata,write_headers,prdt_errors;
    unsigned atapi_port_writes;
    bool no_completion,fail_stop,bad_identify,fail_read,fail_cap_read;
    unsigned fail_at_read_count;
    uint64_t sectors;
    uint64_t last_lba;
};

static void fake_issue(struct fake_ahci *f,uint32_t slots) {
    if(slots!=1U){f->prdt_errors++;return;}
    uint64_t clb=(uint64_t)f->reg[AHCI_PX_CLB/4U]|
                 ((uint64_t)f->reg[(AHCI_PX_CLB+4U)/4U]<<32);
    if(!clb){f->prdt_errors++;return;}
    uint8_t *h=(uint8_t *)(uintptr_t)clb;
    uint32_t flags=get16(h);
    if((flags&0x1fU)!=5U || (flags&(1U<<6)))f->write_headers++;
    if(get16(h+2U)!=1U)f->prdt_errors++;
    const uint8_t *table=(const uint8_t *)(uintptr_t)get64(h+8U);
    if(!table){f->prdt_errors++;return;}
    const uint8_t *fis=table;
    const uint8_t *prdt=table+0x80U;
    if(fis[0]!=0x27U || !(fis[1]&0x80U))f->prdt_errors++;
    uint64_t dma=get64(prdt);
    uint32_t bytes=(get32(prdt+12U)&UINT32_C(0x3fffff))+1U;
    if(!dma){f->prdt_errors++;return;}
    if(fis[2]==ATA_IDENTIFY){
        ++f->identify_count;
        if(bytes!=512U)f->prdt_errors++;
        if(!f->bad_identify) {
            uint8_t *id=(uint8_t *)(uintptr_t)dma;
            memset(id,0,512U);
            put16(id+0U,0x0040U); /* ATA fixed disk. */
            put16(id+83U*2U,1U<<10); /* 48-bit LBA. */
            put16(id+100U*2U,(uint16_t)f->sectors);
            put16(id+101U*2U,(uint16_t)(f->sectors>>16));
            put16(id+102U*2U,(uint16_t)(f->sectors>>32));
            put16(id+103U*2U,(uint16_t)(f->sectors>>48));
        }
    } else if(fis[2]==ATA_READ_DMA_EXT){
        ++f->read_count;
        uint64_t lba=(uint64_t)fis[4]|((uint64_t)fis[5]<<8)|
                     ((uint64_t)fis[6]<<16)|((uint64_t)fis[8]<<24)|
                     ((uint64_t)fis[9]<<32)|((uint64_t)fis[10]<<40);
        f->last_lba=lba;
        unsigned n=(unsigned)fis[12]|((unsigned)fis[13]<<8);
        if(n!=1U || bytes!=512U || lba>=f->sectors)f->prdt_errors++;
        uint8_t *dst=(uint8_t *)(uintptr_t)dma;
        for(unsigned i=0;i<512U;i++)dst[i]=(uint8_t)(i+17U*lba);
        if(f->fail_read || (f->fail_at_read_count &&
            f->read_count==f->fail_at_read_count))
            f->reg[AHCI_PX_IS/4U]|=1U<<30;
    } else ++f->illegal_ata;
    if(!f->no_completion){
        put32(h+4U,bytes); /* AHCI HBA writes PRDBC into command header. */
        f->reg[AHCI_PX_CI/4U]=0U;
    }
}
static int fake_read(void *ctx,uint32_t off,uint32_t *value) {
    struct fake_ahci *f=ctx;
    if(!value || (off&3U) || off>=sizeof(f->reg))return -EIO;
    if(f->fail_cap_read && off==0U)return -EIO;
    if(off==AHCI_PX_CMD && !f->fail_stop){
        /* A previously stalled hardware engine may finally quiesce on a
         * later poll after the injected fault disappears. */
        uint32_t cmd=f->reg[off/4U];
        if(!(cmd&AHCI_CMD_ST))cmd&=~AHCI_CMD_CR;
        if(!(cmd&AHCI_CMD_FRE))cmd&=~AHCI_CMD_FR;
        f->reg[off/4U]=cmd;
        active_dma=(cmd&(AHCI_CMD_CR|AHCI_CMD_FR))!=0U;
    }
    *value=f->reg[off/4U];
    return 0;
}
static int fake_write(void *ctx,uint32_t off,uint32_t value) {
    struct fake_ahci *f=ctx;
    if((off&3U) || off>=sizeof(f->reg))return -EIO;
    if(off>=AHCI_PORT1 && off<AHCI_PORT1+0x80U)
        ++f->atapi_port_writes;
    if(off==AHCI_PX_IS || off==AHCI_PX_SERR){
        f->reg[off/4U]&=~value;
    } else if(off==AHCI_PX_CMD){
        /* CR and FR are hardware-owned read-only status bits. A guest
         * read-modify-write cannot reassert them after ST/FRE are clear. */
        unsigned status=value&~(AHCI_CMD_CR|AHCI_CMD_FR);
        if(value&AHCI_CMD_ST)status|=AHCI_CMD_CR;
        if(value&AHCI_CMD_FRE)status|=AHCI_CMD_FR;
        if(f->fail_stop) status|=f->reg[off/4U]&
                           (AHCI_CMD_CR|AHCI_CMD_FR);
        f->reg[off/4U]=status;
        active_dma=(status&(AHCI_CMD_CR|AHCI_CMD_FR))!=0U;
    } else if(off==AHCI_PX_CI){
        f->reg[off/4U]=value;
        if(value&1U)fake_issue(f,value);
    } else {
        f->reg[off/4U]=value;
    }
    return 0;
}
static void reset(struct fake_ahci *f) {
    for(unsigned i=0;i<allocations;i++){
        if(frames[i])free(frames[i]);
        frames[i]=NULL;
    }
    allocations=frees=0U;
    active_dma=invalid_free=false;
    memset(f,0,sizeof(*f));
    f->sectors=32768U;
    f->reg[0x00U/4U]=AHCI_CAP_S64A|(3U<<8); /* 1 port, 4 slots. */
    f->reg[AHCI_GHC/4U]=1U<<31; /* QEMU AHCI already enabled. */
    f->reg[AHCI_PI/4U]=1U;
    f->reg[0x10U/4U]=0x00010300U; /* AHCI v1.3 */
    f->reg[AHCI_PX_TFD/4U]=0x50U; /* DRDY+DSC; idle. */
    f->reg[AHCI_PX_SIG/4U]=0x00000101U; /* ATA disk. */
    f->reg[AHCI_PX_SSTS/4U]=0x00000103U; /* DET=3, IPM=1. */
}
static int open_session(struct fake_ahci *f,struct nas_ahci_io *io,
                        struct nas_ahci_ro_session *session) {
    *io=(struct nas_ahci_io){.context=f,.mmio_bytes=sizeof(f->reg),
                            .read32=fake_read,.write32=fake_write};
    return nas_ahci_ro_open(io,&nas_dma_qemu_coherent,session);
}

int main(void) {
    struct fake_ahci f;
    struct nas_ahci_io io={0};
    struct nas_ahci_ro_session s={0};
    uint8_t sector[512]={0};
    reset(&f);
    CHECK(open_session(&f,&io,&s)==0);
    CHECK(s.ready && s.sectors==32768U && s.sector_bytes==512U);
    CHECK(f.identify_count==1U && f.illegal_ata==0U);
    CHECK(f.write_headers==0U && f.prdt_errors==0U);
    s.in_io=true;
    CHECK(nas_ahci_ro_read(&s,0U,1U,sector)==-EBUSY);
    CHECK(nas_ahci_ro_close(&s)==-EBUSY);
    s.in_io=false;
    CHECK(nas_ahci_ro_read(&s,0U,1U,sector)==0);
    for(unsigned i=0;i<512U;i++)CHECK(sector[i]==(uint8_t)i);
    CHECK(nas_ahci_ro_read(&s,17U,1U,sector)==0);
    for(unsigned i=0;i<512U;i++)CHECK(sector[i]==(uint8_t)(i+17U*17U));
    uint8_t multi[5U*512U];
    CHECK(nas_ahci_ro_read(&s,100U,5U,multi)==0);
    for(unsigned n=0;n<5U;n++)
        for(unsigned i=0;i<512U;i++)
            CHECK(multi[n*512U+i]==(uint8_t)(i+17U*(100U+n)));
    CHECK(nas_ahci_ro_read(&s,32767U,2U,multi)==-EINVAL);
    CHECK(nas_ahci_ro_read(&s,32768U,1U,sector)==-EINVAL);
    CHECK(nas_ahci_ro_read(&s,0U,0U,sector)==-EINVAL);
    CHECK(f.read_count==7U && f.illegal_ata==0U &&
          f.write_headers==0U && f.prdt_errors==0U);
    int close_rc=nas_ahci_ro_close(&s);
    if(close_rc)fprintf(stderr,"H9 first close rc=%d CMD=%08x active_dma=%d lease_held=%u may_dma=%d\n",
                         close_rc,f.reg[AHCI_PX_CMD/4U],active_dma,
                         s.lease.held,s.lease.may_dma);
    CHECK(close_rc==0);
    CHECK(allocations==frees && !invalid_free);

    /* Q35 includes the boot ISO on another SATA/ATAPI port. One real ATA
     * disk must be selected without changing the ATAPI optical port. */
    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.reg[0U]=(f.reg[0U]&~31U)|1U; /* CAP.NP=1: two ports. */
    f.reg[AHCI_PI/4U]=3U; /* SATA disk + boot optical device. */
    f.reg[AHCI_PORT1_SIG/4U]=0xeb140101U; /* ATAPI signature. */
    f.reg[AHCI_PORT1_SSTS/4U]=0x00000103U;
    CHECK(open_session(&f,&io,&s)==0);
    CHECK(s.port==0U && f.atapi_port_writes==0U);
    CHECK(nas_ahci_ro_read(&s,4U,1U,sector)==0);
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free && f.atapi_port_writes==0U);

    /* An outstanding SATA NCQ command or command-slot bit belongs to
     * another owner. Never overwrite its CLB/FB or adopt its live DMA. */
    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.reg[AHCI_PX_SACT/4U]=1U;
    CHECK(open_session(&f,&io,&s)==-EBUSY);
    CHECK(allocations==0U && !active_dma);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.reg[AHCI_PX_CI/4U]=1U;
    CHECK(open_session(&f,&io,&s)==-EBUSY);
    CHECK(allocations==0U && !active_dma);

    /* Even if the command/FIS engines stop, a persisted PxSACT/PxCI bit
     * must pin all leased pages until it is conclusively cleared. */
    reset(&f);s=(struct nas_ahci_ro_session){0};
    CHECK(open_session(&f,&io,&s)==0);
    f.reg[AHCI_PX_SACT/4U]=1U;
    CHECK(nas_ahci_ro_close(&s)==-EBUSY);
    CHECK(frees==0U && s.lease.held==4U && !invalid_free);
    f.reg[AHCI_PX_SACT/4U]=0U;
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    CHECK(open_session(&f,&io,&s)==0);
    f.reg[AHCI_PX_CI/4U]=1U;
    CHECK(nas_ahci_ro_close(&s)==-EBUSY);
    CHECK(frees==0U && s.lease.held==4U && !invalid_free);
    f.reg[AHCI_PX_CI/4U]=0U;
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    /* Verify the upper 48-bit LBA bytes reach the SATA register FIS,
     * rather than silently truncating to a 28-bit or 32-bit ATA command. */
    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.sectors=UINT64_C(0x100000400);
    CHECK(open_session(&f,&io,&s)==0);
    CHECK(nas_ahci_ro_read(&s,UINT64_C(0x100000040),1U,sector)==0);
    CHECK(f.last_lba==UINT64_C(0x100000040));
    CHECK(f.illegal_ata==0U && f.prdt_errors==0U);
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    /* Multi-sector BlockDevice reads must not report partial success. */
    reset(&f);s=(struct nas_ahci_ro_session){0};
    CHECK(open_session(&f,&io,&s)==0);
    f.fail_at_read_count=2U;
    memset(multi,0xa5,sizeof(multi));
    CHECK(nas_ahci_ro_read(&s,40U,3U,multi)==-EIO);
    CHECK(f.read_count==2U);
    for(unsigned i=0;i<512U;i++)
        CHECK(multi[i]==(uint8_t)(i+17U*40U));
    for(unsigned i=512U;i<sizeof(multi);i++)CHECK(multi[i]==0xa5U);
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.fail_cap_read=true;
    CHECK(open_session(&f,&io,&s)==-EIO);
    CHECK(allocations==0U && !active_dma);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.reg[0U]&=~AHCI_CAP_S64A; /* high DMA bus address is illegal. */
    CHECK(open_session(&f,&io,&s)==-EOPNOTSUPP);
    CHECK(allocations==0U && !active_dma);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.reg[AHCI_PI/4U]=0U;
    CHECK(open_session(&f,&io,&s)!=0);
    CHECK(allocations==0U && !active_dma);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.reg[AHCI_PX_SIG/4U]=0xeb140101U; /* ATAPI, not ATA. */
    CHECK(open_session(&f,&io,&s)!=0);
    CHECK(allocations==0U && !active_dma);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.bad_identify=true;
    CHECK(open_session(&f,&io,&s)!=0);
    CHECK(allocations==frees && !invalid_free);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    CHECK(open_session(&f,&io,&s)==0);
    f.fail_read=true;
    CHECK(nas_ahci_ro_read(&s,0U,1U,sector)==-EIO);
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    f.no_completion=true;
    CHECK(open_session(&f,&io,&s)==-ETIMEDOUT);
    CHECK(s.io && s.may_dma && s.lease.held==4U);
    CHECK(frees==0U && !invalid_free);
    CHECK(nas_ahci_ro_close(&s)==-EBUSY);
    CHECK(frees==0U && !invalid_free);
    f.reg[AHCI_PX_CI/4U]=0U;
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    reset(&f);s=(struct nas_ahci_ro_session){0};
    CHECK(open_session(&f,&io,&s)==0);
    f.fail_stop=true;
    CHECK(nas_ahci_ro_close(&s)==-ETIMEDOUT);
    CHECK(frees==0U && !invalid_free);
    f.fail_stop=false;
    CHECK(nas_ahci_ro_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);

    puts("[H9 AHCI READ-ONLY HOST PASS] IDENTIFY/LBA48, READ DMA EXT, no writes, timeout, CAP.S64A, DMA quiescence");
    return 0;
}
