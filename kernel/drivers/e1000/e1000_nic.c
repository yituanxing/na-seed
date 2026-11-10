#include "readonly.h"
#include "../../errno.h"
#include <stddef.h>
#include <stdint.h>

/* QEMU's Intel 82540EM register and descriptor programming model (Intel
 * 8254x/8257x datasheet, QEMU hw/net/e1000.c). We reuse NaSeed's existing
 * DMA lease, NetDevice and lwIP implementations instead of importing another
 * TCP stack or Linux/U-Boot GPL network driver. The Zephyr Apache-2.0 E1000
 * implementation was reviewed as a protocol reference; its direct CPU-pointer
 * DMA assumptions and unbounded TX waits cannot be brought over unchanged.
 * This QEMU-only, bounded adapter is written independently: exactly one
 * in-flight TX and eight 2 KiB RX buffers bound our DMA attack surface. */
#define REG_CTRL  0x00000U
#define REG_STATUS 0x00008U
#define REG_EERD  0x00014U
#define REG_ICR   0x000c0U
#define REG_IMC   0x000d8U
#define REG_RCTL  0x00100U
#define REG_TCTL  0x00400U
#define REG_TIPG  0x00410U
#define REG_RDBAL 0x02800U
#define REG_RDBAH 0x02804U
#define REG_RDLEN 0x02808U
#define REG_RDH   0x02810U
#define REG_RDT   0x02818U
#define REG_TDBAL 0x03800U
#define REG_TDBAH 0x03804U
#define REG_TDLEN 0x03808U
#define REG_TDH   0x03810U
#define REG_TDT   0x03818U
#define REG_RAL0  0x05400U
#define REG_RAH0  0x05404U

#define CTRL_RST (1U<<26)
#define CTRL_SLU (1U<<6)
#define STATUS_LU (1U<<1)
#define RAH_AV (1U<<31)
#define RCTL_EN (1U<<1)
#define RCTL_BAM (1U<<15)
#define RCTL_SECRC (1U<<26)
#define TCTL_EN (1U<<1)
#define TCTL_PSP (1U<<3)
#define RXD_DD (1U<<0)
#define RXD_EOP (1U<<1)
#define TXD_EOP (1U<<0)
#define TXD_IFCS (1U<<1)
#define TXD_RS (1U<<3)
#define TXD_DD (1U<<0)
#define EERD_START (1U<<0)
#define EERD_DONE (1U<<4)
#define E1000_POLL_LIMIT 1000000U
#define E1000_RX_RING_BYTES (NAS_E1000_RX_DESCRIPTORS*16U)
#define E1000_TX_RING_BYTES (NAS_E1000_TX_DESCRIPTORS*16U)

static void clear_bytes(void *dst,unsigned count) {
    uint8_t *p=(uint8_t *)dst;
    for(unsigned i=0;i<count;i++)p[i]=0;
}
static void copy_bytes(void *dst,const void *src,unsigned count) {
    uint8_t *d=(uint8_t *)dst;
    const uint8_t *s=(const uint8_t *)src;
    for(unsigned i=0;i<count;i++)d[i]=s[i];
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static void put16(uint8_t *p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
static void put64(uint8_t *p,uint64_t v) {
    for(unsigned i=0;i<8U;i++)p[i]=(uint8_t)(v>>(8U*i));
}
static int reg_read(struct nas_e1000_session *s,uint32_t off,uint32_t *v) {
    if(!s || !s->io || !s->io->read32 || !v ||
       (off&3U) || off>s->io->mmio_bytes ||
       s->io->mmio_bytes-off<4U)return -EINVAL;
    return s->io->read32(s->io->context,off,v);
}
static int reg_write(struct nas_e1000_session *s,uint32_t off,uint32_t v) {
    if(!s || !s->io || !s->io->write32 ||
       (off&3U) || off>s->io->mmio_bytes ||
       s->io->mmio_bytes-off<4U)return -EINVAL;
    return s->io->write32(s->io->context,off,v);
}
static int pair_write(struct nas_e1000_session *s,uint32_t low,uint64_t v) {
    int rc=reg_write(s,low,(uint32_t)v);
    return rc ? rc : reg_write(s,low+4U,(uint32_t)(v>>32));
}
static bool page_ok(const struct nas_dma_page *page) {
    return page && page->cpu && page->bytes>=NAS_DMA_PAGE_BYTES &&
           !(page->bus_address&(NAS_DMA_PAGE_BYTES-1U));
}
static bool unicast_mac_valid(uint32_t ral,uint32_t rah) {
    return (rah&RAH_AV) && !((uint8_t)ral&1U) &&
           (ral || (rah&0xffffU)) &&
           !(ral==UINT32_MAX && (rah&0xffffU)==0xffffU);
}
static int eeprom_mac(struct nas_e1000_session *s,
                      uint32_t *ral,uint32_t *rah) {
    uint16_t w[3]={0};
    for(unsigned i=0;i<3U;i++) {
        int rc=reg_write(s,REG_EERD,EERD_START|(i<<8));
        if(rc)return rc;
        bool finished=false;
        for(unsigned poll=0;poll<E1000_POLL_LIMIT;poll++) {
            uint32_t value=0;
            rc=reg_read(s,REG_EERD,&value);
            if(rc)return rc;
            if(value&EERD_DONE) {
                w[i]=(uint16_t)(value>>16);
                finished=true;break;
            }
        }
        if(!finished)return -ETIMEDOUT;
    }
    *ral=(uint32_t)w[0]|((uint32_t)w[1]<<16);
    *rah=(uint32_t)w[2]|RAH_AV;
    if(!unicast_mac_valid(*ral,*rah))return -EOPNOTSUPP;
    int rc=reg_write(s,REG_RAL0,*ral);
    return rc ? rc : reg_write(s,REG_RAH0,*rah);
}
static uint8_t *rx_descriptor(struct nas_e1000_session *s,unsigned index) {
    return (uint8_t *)s->rx_ring.cpu+index*16U;
}
static uint8_t *tx_descriptor(struct nas_e1000_session *s,unsigned index) {
    return (uint8_t *)s->tx_ring.cpu+index*16U;
}
static uint8_t *rx_buffer(struct nas_e1000_session *s,unsigned index) {
    return (uint8_t *)s->rx_pages[index/2U].cpu+
           (index%2U)*NAS_E1000_DMA_BUFFER_BYTES;
}
static struct nas_dma_page *rx_page(struct nas_e1000_session *s,
                                   unsigned index) {
    return &s->rx_pages[index/2U];
}
static uint32_t rx_page_offset(unsigned index) {
    return (index%2U)*NAS_E1000_DMA_BUFFER_BYTES;
}

/* The *only* force-stop authority comes from the caller's fixed QEMU E1000
 * qualification. Never use this on real hardware without an IOMMU and a
 * chipset-specific DMA completion contract. QEMU synchronous reset is the
 * transport's DMA quiescence boundary. No pending DMA is freed on error. */
static int reset_quiesced(struct nas_e1000_session *s) {
    int rc=reg_write(s,REG_IMC,UINT32_MAX);
    uint32_t rctl=0,tctl=0,ctrl=0;
    if(!rc)rc=reg_read(s,REG_RCTL,&rctl);
    if(!rc)rc=reg_read(s,REG_TCTL,&tctl);
    if(!rc)rc=reg_write(s,REG_RCTL,rctl&~RCTL_EN);
    if(!rc)rc=reg_write(s,REG_TCTL,tctl&~TCTL_EN);
    if(!rc)rc=reg_read(s,REG_CTRL,&ctrl);
    if(!rc)rc=reg_write(s,REG_CTRL,ctrl|CTRL_RST);
    if(rc)return rc;
    for(unsigned i=0;i<E1000_POLL_LIMIT;i++) {
        rc=reg_read(s,REG_CTRL,&ctrl);
        if(rc)return rc;
        if(ctrl&CTRL_RST)continue;
        rc=reg_read(s,REG_RCTL,&rctl);
        if(!rc)rc=reg_read(s,REG_TCTL,&tctl);
        if(rc)return rc;
        if(!(rctl&RCTL_EN) && !(tctl&TCTL_EN))return 0;
    }
    return -ETIMEDOUT;
}

int nas_e1000_close(struct nas_e1000_session *s) {
    if(!s || !s->io || !s->lease.provider)return -EINVAL;
    if(__atomic_exchange_n(&s->closing,true,__ATOMIC_ACQ_REL))
        return -EBUSY;
    if(__atomic_load_n(&s->in_tx,__ATOMIC_ACQUIRE) ||
       __atomic_load_n(&s->in_rx_poll,__ATOMIC_ACQUIRE) ||
       __atomic_load_n(&s->in_irq,__ATOMIC_ACQUIRE)) {
        __atomic_store_n(&s->closing,false,__ATOMIC_RELEASE);
        return -EBUSY;
    }
    s->ready=false;
    int rc=0;
    if(s->may_dma) {
        rc=reset_quiesced(s);
        if(rc)goto fail; /* Leave all descriptor/data DMA pages pinned. */
        rc=nas_dma_lease_disarm_after_quiesce(&s->lease);
        if(rc)goto fail;
        s->may_dma=false;
    }
    struct nas_dma_page *pages[]={
        &s->tx_page,&s->rx_pages[3],&s->rx_pages[2],
        &s->rx_pages[1],&s->rx_pages[0],&s->tx_ring,&s->rx_ring
    };
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        if(pages[i]->cpu) {
            rc=nas_dma_lease_free_page(&s->lease,pages[i]);
            if(rc)goto fail;
        }
    }
    rc=nas_dma_lease_end(&s->lease);
    if(rc)goto fail;
    *s=(struct nas_e1000_session){0};
    return 0;
fail:
    __atomic_store_n(&s->closing,false,__ATOMIC_RELEASE);
    return rc;
}

int nas_e1000_open(const struct nas_e1000_io *io,
                   const struct nas_dma_provider *dma,
                   struct nas_e1000_session *s) {
    if(!io || !io->read32 || !io->write32 ||
       io->mmio_bytes<NAS_E1000_MMIO_BYTES || !dma ||
       !dma->alloc_page || !dma->release_page ||
       (!dma->coherent && !dma->sync) || !s)return -EINVAL;
    if(s->io || s->lease.provider || s->rx_ring.cpu || s->tx_ring.cpu ||
       s->tx_page.cpu)return -EBUSY;
    *s=(struct nas_e1000_session){.io=io,.dma=dma};
    int rc=nas_dma_lease_begin(&s->lease,dma);
    if(rc){*s=(struct nas_e1000_session){0};return rc;}
    /* Reset is before any of our DMA mappings are programmed. It clears
     * abandoned firmware descriptors and disables all interrupts. */
    rc=reset_quiesced(s);
    if(rc)goto fail;
    uint32_t ral=0,rah=0;
    rc=reg_read(s,REG_RAL0,&ral);
    if(!rc)rc=reg_read(s,REG_RAH0,&rah);
    if(rc)goto fail;
    if(!unicast_mac_valid(ral,rah)) {
        rc=eeprom_mac(s,&ral,&rah);
        if(rc)goto fail;
    }
    for(unsigned i=0;i<4U;i++)s->mac[i]=(uint8_t)(ral>>(i*8U));
    s->mac[4]=(uint8_t)rah;
    s->mac[5]=(uint8_t)(rah>>8);

    struct nas_dma_page *pages[]={
        &s->rx_ring,&s->tx_ring,&s->rx_pages[0],&s->rx_pages[1],
        &s->rx_pages[2],&s->rx_pages[3],&s->tx_page
    };
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++) {
        rc=nas_dma_lease_alloc_page(&s->lease,pages[i]);
        if(rc)goto fail;
        if(!page_ok(pages[i])){rc=-EOPNOTSUPP;goto fail;}
        clear_bytes(pages[i]->cpu,NAS_DMA_PAGE_BYTES);
    }
    for(unsigned i=0;i<NAS_E1000_RX_DESCRIPTORS;i++) {
        uint8_t *d=rx_descriptor(s,i);
        put64(d,rx_page(s,i)->bus_address+rx_page_offset(i));
    }
    rc=nas_dma_lease_sync(&s->lease,&s->rx_ring,0,
                          E1000_RX_RING_BYTES,NAS_DMA_TO_DEVICE);
    if(!rc)rc=nas_dma_lease_sync(&s->lease,&s->tx_ring,0,
                                 E1000_TX_RING_BYTES,NAS_DMA_TO_DEVICE);
    for(unsigned i=0;!rc && i<NAS_E1000_RX_BUFFER_PAGES;i++)
        rc=nas_dma_lease_sync(&s->lease,&s->rx_pages[i],0,
                               NAS_DMA_PAGE_BYTES,NAS_DMA_FROM_DEVICE);
    if(rc)goto fail;
    rc=pair_write(s,REG_RDBAL,s->rx_ring.bus_address);
    if(!rc)rc=reg_write(s,REG_RDLEN,E1000_RX_RING_BYTES);
    if(!rc)rc=reg_write(s,REG_RDH,0U);
    if(!rc)rc=reg_write(s,REG_RDT,NAS_E1000_RX_DESCRIPTORS-1U);
    if(!rc)rc=pair_write(s,REG_TDBAL,s->tx_ring.bus_address);
    if(!rc)rc=reg_write(s,REG_TDLEN,E1000_TX_RING_BYTES);
    if(!rc)rc=reg_write(s,REG_TDH,0U);
    if(!rc)rc=reg_write(s,REG_TDT,0U);
    if(!rc)rc=reg_write(s,REG_TIPG,10U|(8U<<10)|(6U<<20));
    if(rc)goto fail;
    /* Arm BEFORE hardware sees RCTL.EN/TCTL.EN. A partial setup still
     * requires a verified reset before DMA buffers are ever reclaimed. */
    rc=nas_dma_lease_arm(&s->lease);
    if(rc)goto fail;
    s->may_dma=true;
    uint32_t ctrl=0;
    rc=reg_read(s,REG_CTRL,&ctrl);
    if(!rc)rc=reg_write(s,REG_CTRL,ctrl|CTRL_SLU);
    if(!rc)rc=reg_write(s,REG_RCTL,RCTL_EN|RCTL_BAM|RCTL_SECRC);
    if(!rc)rc=reg_write(s,REG_TCTL,TCTL_EN|TCTL_PSP|
                                     (15U<<4)|(64U<<12));
    if(!rc)rc=reg_write(s,REG_IMC,UINT32_MAX);
    if(rc)goto fail;
    s->ready=true;
    return 0;
fail:
    (void)nas_e1000_close(s);
    return rc;
}

/* Keep at most one descriptor pending, while the eight-slot hardware ring
 * rotates so TDH/TDT stay coherent. No wait, sleeping or unbounded retry
 * occurs in the networking worker. Backpressure is explicit -EAGAIN. */
int nas_e1000_transmit(void *backend,const uint8_t *frame,uint32_t bytes) {
    struct nas_e1000_session *s=backend;
    if(!s || !frame || bytes<14U || bytes>NAS_E1000_FRAME_MAX)
        return -EINVAL;
    if(__atomic_exchange_n(&s->in_tx,true,__ATOMIC_ACQUIRE))
        return -EAGAIN;
    int rc=0;
    if(!s->ready || !s->may_dma || s->closing){rc=-ENODEV;goto done;}
    if(s->tx_pending) {
        uint8_t *d=tx_descriptor(s,s->tx_head);
        rc=nas_dma_lease_sync(&s->lease,&s->tx_ring,
                              (uint32_t)s->tx_head*16U,16U,
                              NAS_DMA_FROM_DEVICE);
        if(rc)goto done;
        if(!(d[12]&TXD_DD)){rc=-EAGAIN;goto done;}
        s->tx_pending=false;
        s->tx_head=(uint8_t)((s->tx_head+1U)%NAS_E1000_TX_DESCRIPTORS);
    }
    const unsigned idx=s->tx_head;
    uint8_t *d=tx_descriptor(s,idx);
    const uint32_t wire_bytes=bytes<60U ? 60U : bytes;
    /* ARP, DHCP and small TCP ACK frames are often shorter than 60 bytes.
     * Pad explicitly rather than depending on each E1000 emulation's PSP
     * handling, and keep the extra data private to the DMA bounce buffer. */
    clear_bytes(s->tx_page.cpu,wire_bytes);
    copy_bytes(s->tx_page.cpu,frame,bytes);
    clear_bytes(d,16U);
    put64(d,s->tx_page.bus_address);
    put16(d+8U,(uint16_t)wire_bytes);
    d[11]=(uint8_t)(TXD_EOP|TXD_IFCS|TXD_RS);
    rc=nas_dma_lease_sync(&s->lease,&s->tx_page,0U,wire_bytes,
                          NAS_DMA_TO_DEVICE);
    if(!rc)rc=nas_dma_lease_sync(&s->lease,&s->tx_ring,
                                 (uint32_t)idx*16U,16U,
                                 NAS_DMA_TO_DEVICE);
    if(!rc)rc=reg_write(s,REG_TDT,(idx+1U)%NAS_E1000_TX_DESCRIPTORS);
    if(rc){s->ready=false;goto done;}
    s->tx_pending=true;
done:
    __atomic_store_n(&s->in_tx,false,__ATOMIC_RELEASE);
    return rc;
}

int nas_e1000_poll(void *backend,uint32_t budget,
                   nas_netdev_rx receive,void *consumer) {
    struct nas_e1000_session *s=backend;
    if(!s || !receive)return -EINVAL;
    if(__atomic_exchange_n(&s->in_rx_poll,true,__ATOMIC_ACQUIRE))
        return -EAGAIN;
    int rc=0,consumed=0;
    if(!s->ready || !s->may_dma || s->closing){rc=-ENODEV;goto done;}
    if(budget>NAS_E1000_RX_DESCRIPTORS)
        budget=NAS_E1000_RX_DESCRIPTORS;
    for(uint32_t i=0;i<budget;i++) {
        const unsigned idx=s->rx_head;
        uint8_t *d=rx_descriptor(s,idx);
        rc=nas_dma_lease_sync(&s->lease,&s->rx_ring,idx*16U,16U,
                              NAS_DMA_FROM_DEVICE);
        if(rc)goto poison;
        if(!(d[12]&RXD_DD))break;
        const uint16_t length=get16(d+8U);
        const bool good=(d[12]&RXD_EOP) && !d[13] &&
                         length>=14U && length<=NAS_E1000_FRAME_MAX;
        if(good) {
            rc=nas_dma_lease_sync(&s->lease,rx_page(s,idx),
                                  rx_page_offset(idx),length,
                                  NAS_DMA_FROM_DEVICE);
            if(rc)goto poison;
            /* Frame buffer is live only until receive() returns. Unlike
             * the netdev interface, the ring is hardware-owned again
             * after advancing RDT below. */
            rc=receive(consumer,rx_buffer(s,idx),length);
            if(rc<0){/* Still recycle the consumed descriptor safely. */}
        }
        d[8]=d[9]=d[10]=d[11]=d[12]=d[13]=d[14]=d[15]=0U;
        int sync=nas_dma_lease_sync(&s->lease,&s->rx_ring,idx*16U,16U,
                                    NAS_DMA_TO_DEVICE);
        if(sync){rc=sync;goto poison;}
        sync=nas_dma_lease_sync(&s->lease,rx_page(s,idx),
                                rx_page_offset(idx),NAS_E1000_DMA_BUFFER_BYTES,
                                NAS_DMA_FROM_DEVICE);
        if(sync){rc=sync;goto poison;}
        sync=reg_write(s,REG_RDT,idx);
        if(sync){rc=sync;goto poison;}
        s->rx_head=(uint8_t)((idx+1U)%NAS_E1000_RX_DESCRIPTORS);
        consumed++;
        if(rc<0)goto done;
    }
    rc=consumed;
    goto done;
poison:
    s->ready=false;
done:
    __atomic_store_n(&s->in_rx_poll,false,__ATOMIC_RELEASE);
    return rc;
}

bool nas_e1000_link_up(void *backend) {
    struct nas_e1000_session *s=backend;
    if(!s || !s->ready || s->closing)return false;
    uint32_t status=0;
    return !reg_read(s,REG_STATUS,&status) && (status&STATUS_LU)!=0U;
}

int nas_e1000_irq_ack(struct nas_e1000_session *s) {
    if(!s)return -EINVAL;
    if(__atomic_exchange_n(&s->in_irq,true,__ATOMIC_ACQUIRE))
        return -EAGAIN;
    uint32_t cause=0;
    int rc=s->ready && !s->closing ?
           reg_read(s,REG_ICR,&cause) : -ENODEV;
    __atomic_store_n(&s->in_irq,false,__ATOMIC_RELEASE);
    return rc ? rc : (int)(cause&0x7fffffffU);
}
