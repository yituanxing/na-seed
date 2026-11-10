#include "readonly.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>

/* AHCI 1.x / SATA register/FIS protocol. The PCI and QEMU qualification
 * belong to the caller; this bounded transport core never scans an unknown
 * host or copies any third-party driver's implementation. */
#define AHCI_CAP 0x00U
#define AHCI_GHC 0x04U
#define AHCI_PI 0x0cU
#define AHCI_VS 0x10U
#define AHCI_PORTS 0x100U
#define AHCI_PORT_BYTES 0x80U
#define AHCI_CAP_S64A (1U<<31)
#define AHCI_GHC_AE (1U<<31)
#define AHCI_CLB 0x00U
#define AHCI_CLBU 0x04U
#define AHCI_FB 0x08U
#define AHCI_FBU 0x0cU
#define AHCI_IS 0x10U
#define AHCI_IE 0x14U
#define AHCI_CMD 0x18U
#define AHCI_TFD 0x20U
#define AHCI_SIG 0x24U
#define AHCI_SSTS 0x28U
#define AHCI_SERR 0x30U
#define AHCI_SACT 0x34U
#define AHCI_CI 0x38U
#define AHCI_CMD_ST (1U<<0)
#define AHCI_CMD_FRE (1U<<4)
#define AHCI_CMD_FR (1U<<14)
#define AHCI_CMD_CR (1U<<15)
#define AHCI_TFD_ERR (1U<<0)
#define AHCI_TFD_DRQ (1U<<3)
#define AHCI_TFD_BSY (1U<<7)
#define AHCI_IS_TFES (1U<<30)
#define AHCI_SIG_ATA 0x00000101U
#define AHCI_FIS_REG_H2D 0x27U
#define AHCI_FIS_COMMAND 0x80U
#define AHCI_ATA_IDENTIFY 0xecU
#define AHCI_ATA_READ_DMA_EXT 0x25U
#define AHCI_POLL_LIMIT 1000000U
#define AHCI_ONE_SLOT 1U
#define AHCI_DATA_BYTES 512U

static void zero_bytes(void *destination,uint32_t count) {
    uint8_t *p=(uint8_t *)destination;
    for(uint32_t i=0;i<count;i++)p[i]=0;
}
static void copy_bytes(void *destination,const void *source,uint32_t count) {
    uint8_t *d=(uint8_t *)destination;
    const uint8_t *s=(const uint8_t *)source;
    for(uint32_t i=0;i<count;i++)d[i]=s[i];
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static int io_read(struct nas_ahci_ro_session *s,uint32_t off,uint32_t *v) {
    if(!s || !s->io || !s->io->read32 || !v ||
       (off&3U) || off>s->io->mmio_bytes ||
       s->io->mmio_bytes-off<4U)return -EINVAL;
    return s->io->read32(s->io->context,off,v);
}
static int io_write(struct nas_ahci_ro_session *s,uint32_t off,uint32_t v) {
    if(!s || !s->io || !s->io->write32 ||
       (off&3U) || off>s->io->mmio_bytes ||
       s->io->mmio_bytes-off<4U)return -EINVAL;
    return s->io->write32(s->io->context,off,v);
}
static int port_read(struct nas_ahci_ro_session *s,uint32_t off,uint32_t *v) {
    return io_read(s,s->port_offset+off,v);
}
static int port_write(struct nas_ahci_ro_session *s,uint32_t off,uint32_t v) {
    return io_write(s,s->port_offset+off,v);
}
static int port_addr(struct nas_ahci_ro_session *s,
                     uint32_t low,uint64_t addr) {
    int rc=port_write(s,low,(uint32_t)addr);
    if(!rc)rc=port_write(s,low+4U,(uint32_t)(addr>>32));
    return rc;
}

/* First stop command fetching (ST/CR), then the receive FIS engine
 * (FRE/FR). Observing both engines stopped, not merely writing PxCMD,
 * is required before unpinning the controller's DMA page lease. */
static int stop_engines(struct nas_ahci_ro_session *s) {
    uint32_t cmd=0;
    int rc=port_read(s,AHCI_CMD,&cmd);
    if(rc)return rc;
    if(cmd&AHCI_CMD_ST) {
        rc=port_write(s,AHCI_CMD,cmd&~AHCI_CMD_ST);
        if(rc)return rc;
    }
    bool stopped=false;
    for(unsigned i=0;i<AHCI_POLL_LIMIT;i++) {
        rc=port_read(s,AHCI_CMD,&cmd);
        if(rc)return rc;
        if(!(cmd&(AHCI_CMD_ST|AHCI_CMD_CR))) {
            stopped=true;
            break;
        }
    }
    if(!stopped)return -ETIMEDOUT;
    if(cmd&AHCI_CMD_FRE) {
        rc=port_write(s,AHCI_CMD,cmd&~AHCI_CMD_FRE);
        if(rc)return rc;
    }
    stopped=false;
    for(unsigned i=0;i<AHCI_POLL_LIMIT;i++) {
        rc=port_read(s,AHCI_CMD,&cmd);
        if(rc)return rc;
        if(!(cmd&(AHCI_CMD_ST|AHCI_CMD_CR|AHCI_CMD_FRE|AHCI_CMD_FR))) {
            stopped=true;
            break;
        }
    }
    if(!stopped)return -ETIMEDOUT;

    /* A quiesced command/FIS engine is necessary but not sufficient to
     * reclaim its DMA descriptions: PxCI may still contain a submitted
     * command and PxSACT may hold firmware-owned NCQ slots. Refuse both
     * CLB/FB reprogramming (open) and lease disarm (close) until the HBA
     * reports NO outstanding requests. A timeout never waives this gate. */
    uint32_t sact=0,ci=0;
    rc=port_read(s,AHCI_SACT,&sact);
    if(!rc)rc=port_read(s,AHCI_CI,&ci);
    if(rc)return rc;
    return (sact || ci) ? -EBUSY : 0;
}

static bool address_supported(uint32_t cap,const struct nas_dma_page *p) {
    return p->cpu && !(p->bus_address&4095U) &&
           ((cap&AHCI_CAP_S64A) || (p->bus_address>>32)==0U);
}

/* A command occupies slot zero: 1 KiB command-list page, 256-byte received
 * FIS area, a separately aligned command-table page with one 16-byte PRDT,
 * and an independent bounce page. No untrusted disk length reaches DMA. */
static int issue(struct nas_ahci_ro_session *s,uint8_t opcode,
                 uint64_t lba,uint16_t sectors,uint32_t bytes) {
    if(!s || !s->may_dma || !s->ready || !sectors || bytes!=AHCI_DATA_BYTES)
        return -EINVAL;
    uint32_t active=0;
    int rc=port_read(s,AHCI_CI,&active);
    if(rc)return rc;
    if(active)return -EBUSY;
    uint32_t tfd=0;
    bool not_busy=false;
    for(unsigned i=0;i<AHCI_POLL_LIMIT;i++) {
        rc=port_read(s,AHCI_TFD,&tfd);
        if(rc)return rc;
        if(!(tfd&(AHCI_TFD_BSY|AHCI_TFD_DRQ))) {
            not_busy=true;break;
        }
    }
    if(!not_busy)return -ETIMEDOUT;

    uint8_t *header=(uint8_t *)s->cmd_list.cpu;
    uint8_t *table=(uint8_t *)s->cmd_table.cpu;
    zero_bytes(header,32U);
    zero_bytes(table,144U);
    /* DW0: FIS length=5 DWords, W=0 (device-to-host), PRDTL=1.
     * DW2/3: one 128-byte-aligned command table. */
    put32(header,5U|(1U<<16));
    put32(header+8U,(uint32_t)s->cmd_table.bus_address);
    put32(header+12U,(uint32_t)(s->cmd_table.bus_address>>32));
    /* One PRDT: 512 bytes, byte count is zero based, no IOC interrupt.
     * Polling uses PxCI and TFES; hardware never writes guest kernel memory
     * except the controller-owned command FIS/PRDBC and this bounce page. */
    put32(table+128U,(uint32_t)s->bounce.bus_address);
    put32(table+132U,(uint32_t)(s->bounce.bus_address>>32));
    put32(table+140U,bytes-1U);

    /* 20-byte command-register FIS (H2D): 48-bit LBA, LBA mode,
     * ATA IDENTIFY or READ DMA EXT. Always one sector/command. */
    table[0]=AHCI_FIS_REG_H2D;
    table[1]=AHCI_FIS_COMMAND;
    table[2]=opcode;
    table[4]=(uint8_t)lba;
    table[5]=(uint8_t)(lba>>8);
    table[6]=(uint8_t)(lba>>16);
    table[7]=0x40U;
    table[8]=(uint8_t)(lba>>24);
    table[9]=(uint8_t)(lba>>32);
    table[10]=(uint8_t)(lba>>40);
    table[12]=(uint8_t)sectors;
    table[13]=(uint8_t)(sectors>>8);

    rc=nas_dma_lease_sync(&s->lease,&s->cmd_table,0U,144U,
                          NAS_DMA_TO_DEVICE);
    if(!rc)rc=nas_dma_lease_sync(&s->lease,&s->cmd_list,0U,32U,
                                 NAS_DMA_TO_DEVICE);
    if(!rc)rc=nas_dma_lease_sync(&s->lease,&s->bounce,0U,bytes,
                                 NAS_DMA_FROM_DEVICE);
    if(!rc)rc=port_write(s,AHCI_IS,UINT32_MAX); /* W1C */
    if(!rc)rc=port_write(s,AHCI_CI,AHCI_ONE_SLOT);
    if(rc)return rc;
    for(unsigned i=0;i<AHCI_POLL_LIMIT;i++) {
        uint32_t is=0,ci=0;
        rc=port_read(s,AHCI_IS,&is);
        if(!rc)rc=port_read(s,AHCI_CI,&ci);
        if(rc)return rc;
        if(is&AHCI_IS_TFES)return -EIO;
        if(ci&AHCI_ONE_SLOT)continue;
        rc=port_read(s,AHCI_TFD,&tfd);
        if(rc)return rc;
        if(tfd&AHCI_TFD_ERR)return -EIO;
        rc=nas_dma_lease_sync(&s->lease,&s->cmd_list,0U,32U,
                              NAS_DMA_FROM_DEVICE);
        if(rc)return rc;
        if(le32(header+4U)!=bytes)return -EIO; /* PRDBC */
        return nas_dma_lease_sync(&s->lease,&s->bounce,0U,bytes,
                                  NAS_DMA_FROM_DEVICE);
    }
    return -ETIMEDOUT;
}

int nas_ahci_ro_close(struct nas_ahci_ro_session *s) {
    if(!s)return -EINVAL;
    if(__atomic_exchange_n(&s->in_io,true,__ATOMIC_ACQUIRE))
        return -EBUSY;
    if(!s->io || !s->dma || !s->lease.provider) {
        __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
        return -EINVAL;
    }
    s->ready=false;
    int rc=0;
    if(s->may_dma) {
        rc=stop_engines(s);
        if(rc)goto failed; /* Unsafe stop: all DMA pages remain PINNED. */
        rc=nas_dma_lease_disarm_after_quiesce(&s->lease);
        if(rc)goto failed;
        s->may_dma=false;
    }
    struct nas_dma_page *pages[]={&s->bounce,&s->cmd_table,
                                  &s->received_fis,&s->cmd_list};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        if(pages[i]->cpu) {
            rc=nas_dma_lease_free_page(&s->lease,pages[i]);
            if(rc)goto failed;
        }
    }
    rc=nas_dma_lease_end(&s->lease);
    if(rc)goto failed;
    *s=(struct nas_ahci_ro_session){0};
    return 0;
failed:
    __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
    return rc;
}

int nas_ahci_ro_open(const struct nas_ahci_io *io,
                     const struct nas_dma_provider *dma,
                     struct nas_ahci_ro_session *s) {
    if(!io || !io->read32 || !io->write32 || !dma ||
       !dma->alloc_page || !dma->release_page ||
       (!dma->coherent && !dma->sync) || !s ||
       io->mmio_bytes<AHCI_PORTS+AHCI_PORT_BYTES)return -EINVAL;
    if(s->io || s->lease.provider || s->cmd_list.cpu || s->received_fis.cpu ||
       s->cmd_table.cpu || s->bounce.cpu)return -EBUSY;
    *s=(struct nas_ahci_ro_session){.io=io,.dma=dma};
    int rc=nas_dma_lease_begin(&s->lease,dma);
    if(rc) {
        *s=(struct nas_ahci_ro_session){0};
        return rc;
    }
    uint32_t cap=0,ghc=0,pi=0,vs=0;
    rc=io_read(s,AHCI_CAP,&cap);
    if(!rc)rc=io_read(s,AHCI_GHC,&ghc);
    if(!rc)rc=io_read(s,AHCI_PI,&pi);
    if(!rc)rc=io_read(s,AHCI_VS,&vs);
    if(rc)goto failed;
    unsigned port_count=(cap&31U)+1U;
    /* The QEMU-only acceptance profile requires a full 64-bit DMA-capable
     * HBA. This avoids relying on low-address identity DMA allocation
     * before an IOMMU/allocator constraint interface exists. */
    if((vs>>16)!=1U || !(ghc&AHCI_GHC_AE) ||
       !(cap&AHCI_CAP_S64A) || !pi ||
       (port_count<32U && (pi>>port_count)!=0U)) {
        rc=-EOPNOTSUPP;goto failed;
    }
    unsigned disks=0;
    for(unsigned p=0;p<port_count;p++) {
        if(!(pi&(1U<<p)))continue;
        uint32_t off=AHCI_PORTS+(uint32_t)p*AHCI_PORT_BYTES;
        if(off>io->mmio_bytes || io->mmio_bytes-off<AHCI_PORT_BYTES) {
            rc=-EOPNOTSUPP;goto failed;
        }
        uint32_t ssts=0,sig=0;
        rc=io_read(s,off+AHCI_SSTS,&ssts);
        if(!rc)rc=io_read(s,off+AHCI_SIG,&sig);
        if(rc)goto failed;
        /* This is a FIXED Q35 fixture: the readonly data disk uses ide.0
         * (AHCI port zero), while the UEFI boot CDROM is another, ATAPI
         * port. QEMU may report PxSIG=0xffffffff on an unused SATA port
         * until FRE receives its first D2H FIS. Read-only inventory may
         * provisionally select ONLY port zero in that state; below, after
         * enabling FRE, the final SATA signature MUST be proven before
         * sending even the read-only ATA IDENTIFY command. */
        bool present=(ssts&15U)==3U && ((ssts>>8)&15U)==1U;
        if(!present)continue;
        if(p==0U && (sig==AHCI_SIG_ATA || sig==UINT32_MAX)) {
            if(++disks!=1U){rc=-EOPNOTSUPP;goto failed;}
            s->port=0U;
            s->port_offset=off;
        } else if(sig==AHCI_SIG_ATA) {
            /* An additional real ATA data disk violates fixed media
             * ownership. Unrecognized other ports are never modified. */
            rc=-EOPNOTSUPP;goto failed;
        }
    }
    if(disks!=1U){rc=-ENODEV;goto failed;}
    rc=stop_engines(s);
    if(rc)goto failed;

    struct nas_dma_page *pages[]={&s->cmd_list,&s->received_fis,
                                  &s->cmd_table,&s->bounce};
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        rc=nas_dma_lease_alloc_page(&s->lease,pages[i]);
        if(rc)goto failed;
        if(!address_supported(cap,pages[i])){rc=-EOPNOTSUPP;goto failed;}
        zero_bytes(pages[i]->cpu,NAS_DMA_PAGE_BYTES);
    }
    rc=port_addr(s,AHCI_CLB,s->cmd_list.bus_address);
    if(!rc)rc=port_addr(s,AHCI_FB,s->received_fis.bus_address);
    if(!rc)rc=port_write(s,AHCI_IE,0U);
    if(!rc)rc=port_write(s,AHCI_IS,UINT32_MAX);
    if(!rc)rc=port_write(s,AHCI_SERR,UINT32_MAX);
    if(rc)goto failed;

    /* The FIS receive engine may DMA as soon as FRE is set. Arm the
     * software lease before even the first enable write, so failure or
     * timeout cannot release a still-bus-owned descriptor. */
    rc=nas_dma_lease_arm(&s->lease);
    if(rc)goto failed;
    s->may_dma=true;
    uint32_t cmd=0;
    rc=port_read(s,AHCI_CMD,&cmd);
    if(!rc)rc=port_write(s,AHCI_CMD,cmd|AHCI_CMD_FRE);
    if(rc)goto failed;
    /* QEMU publishes the type signature on the initial D2H FIS after
     * FRE is armed. A pre-FRE placeholder must never authorize commands
     * to an ATAPI or unknown target. The same rule safely handles a port
     * whose firmware already initialized the signature before takeover. */
    bool disk_confirmed=false;
    for(unsigned i=0;i<AHCI_POLL_LIMIT;i++) {
        uint32_t sig=0;
        rc=port_read(s,AHCI_SIG,&sig);
        if(rc)goto failed;
        if(sig==AHCI_SIG_ATA){disk_confirmed=true;break;}
        if(sig!=UINT32_MAX)break;
    }
    if(!disk_confirmed){rc=-ENODEV;goto failed;}
    rc=port_read(s,AHCI_CMD,&cmd);
    if(!rc)rc=port_write(s,AHCI_CMD,cmd|AHCI_CMD_ST);
    if(rc)goto failed;
    s->ready=true;

    rc=issue(s,AHCI_ATA_IDENTIFY,0U,1U,AHCI_DATA_BYTES);
    if(rc)goto failed;
    const uint8_t *identify=(const uint8_t *)s->bounce.cpu;
    const uint16_t features=le16(identify+83U*2U);
    const uint16_t sector_info=le16(identify+106U*2U);
    uint64_t sectors=(uint64_t)le16(identify+100U*2U)|
                     ((uint64_t)le16(identify+101U*2U)<<16)|
                     ((uint64_t)le16(identify+102U*2U)<<32)|
                     ((uint64_t)le16(identify+103U*2U)<<48);
    /* AHCI uses one 512-byte PRDT bounce. A disk advertising a larger
     * logical sector via word 106 needs a separate supported LBA format. */
    if((le16(identify)&(1U<<15)) ||
       ((sector_info&(1U<<14)) && (sector_info&(1U<<12))) ||
       !(features&(1U<<10)) || sectors<16384U ||
       (sectors>>48)!=0U){rc=-EOPNOTSUPP;goto failed;}
    s->sectors=sectors;
    s->sector_bytes=AHCI_DATA_BYTES;
    return 0;
failed:
    (void)nas_ahci_ro_close(s);
    return rc;
}

int nas_ahci_ro_read(struct nas_ahci_ro_session *s,uint64_t lba,
                     uint32_t count,void *dst) {
    if(!s || !dst || !count)return -EINVAL;
    if(__atomic_exchange_n(&s->in_io,true,__ATOMIC_ACQUIRE))
        return -EBUSY;
    if(!s->ready || !s->may_dma || !s->bounce.cpu ||
       s->sector_bytes!=AHCI_DATA_BYTES || lba>=s->sectors ||
       (uint64_t)count>s->sectors-lba
#if SIZE_MAX == UINT32_MAX
       || count>SIZE_MAX/AHCI_DATA_BYTES
#endif
       ) {
        __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
        return -EINVAL;
    }
    int rc=0;
    for(uint32_t i=0;i<count;i++) {
        zero_bytes(s->bounce.cpu,AHCI_DATA_BYTES);
        rc=issue(s,AHCI_ATA_READ_DMA_EXT,lba+(uint64_t)i,1U,
                 AHCI_DATA_BYTES);
        if(rc)goto failed;
        copy_bytes((uint8_t *)dst+(size_t)i*AHCI_DATA_BYTES,
                   s->bounce.cpu,AHCI_DATA_BYTES);
    }
    __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
    return 0;
failed:
    /* A timed-out command cannot be resubmitted; close still owns the
     * engine stop and must verify CR/FR before releasing any DMA page. */
    s->ready=false;
    __atomic_store_n(&s->in_io,false,__ATOMIC_RELEASE);
    return rc;
}
