/* Isolated register/DMA model of Intel E1000 (82540EM) for H10.
 * A host fake can reject descriptor ownership regressions, but cannot
 * establish that a real QEMU NIC sent a frame to lwIP or the network.
 * The H10 QEMU guest+host HTTP acceptance is the final authority. */
#include "../kernel/drivers/e1000/readonly.h"
#include "../kernel/platform/x86_h10_e1000.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if(!(expr)) { \
    fprintf(stderr,"[H10 E1000 HOST FAIL] line %u: %s\n",__LINE__,#expr); \
    return 1; \
} } while(0)
#define REG_CTRL   0x0000U
#define REG_STATUS 0x0008U
#define REG_EERD   0x0014U
#define REG_ICR    0x00c0U
#define REG_IMS    0x00d0U
#define REG_IMC    0x00d8U
#define REG_RCTL   0x0100U
#define REG_TCTL   0x0400U
#define REG_RDBAL  0x2800U
#define REG_RDBAH  0x2804U
#define REG_RDLEN  0x2808U
#define REG_RDH    0x2810U
#define REG_RDT    0x2818U
#define REG_TDBAL  0x3800U
#define REG_TDBAH  0x3804U
#define REG_TDLEN  0x3808U
#define REG_TDH    0x3810U
#define REG_TDT    0x3818U
#define REG_RAL0   0x5400U
#define REG_RAH0   0x5404U
#define CTRL_RST   (1U<<26)
#define RCTL_EN    (1U<<1)
#define TCTL_EN    (1U<<1)
#define RX_DD      (1U<<0)
#define RX_EOP     (1U<<1)
#define TX_DD      (1U<<0)
#define TX_EOP     (1U<<0)
#define TX_IFCS    (1U<<1)
#define E1000_REG_BYTES 0x20000U
#define MAX_FRAMES 64U

static void *frames[32];
static unsigned allocations,frees;
static bool dma_live,invalid_free;
uint64_t nas_frame_alloc(void) {
    if(allocations>=32U)return 0;
    void *p=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    if(!p)return 0;
    memset(p,0,NAS_DMA_PAGE_BYTES);
    frames[allocations++]=p;
    return (uint64_t)(uintptr_t)p;
}
uint64_t nas_hhdm_offset(void) {return 0;}
bool nas_frame_put(uint64_t pa) {
    if(dma_live) {invalid_free=true;return false;}
    for(unsigned i=0;i<allocations;i++) {
        if(frames[i]==(void *)(uintptr_t)pa) {
            free(frames[i]);frames[i]=NULL;frees++;return true;
        }
    }
    return false;
}
static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p+4U)<<32);
}
static void put16(uint8_t *p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
}
struct fake_e1000 {
    uint32_t r[E1000_REG_BYTES/4U];
    uint8_t tx[MAX_FRAMES][2048];
    uint32_t tx_bytes[MAX_FRAMES];
    unsigned tx_count,rx_count,malformed_tx,illegal_mmio_writes;
    bool withhold_tx_completion,withhold_reset_completion;
    bool no_link,bad_eeprom,stuck_eerd,fail_tctl_enable;
};
static uint32_t fake_reg(const struct fake_e1000 *f,uint32_t off) {
    return f->r[off/4U];
}
static uint64_t dma_ring_addr(const struct fake_e1000 *f,
                              uint32_t lo,uint32_t hi) {
    return (uint64_t)fake_reg(f,lo) |
           ((uint64_t)fake_reg(f,hi)<<32);
}
/* Intel E1000 legacy TX descriptor: 64-bit buf, len, cso, cmd, status. */
static void fake_tx_execute(struct fake_e1000 *f,uint32_t tail) {
    if(f->withhold_tx_completion)return;
    uint32_t ringbytes=fake_reg(f,REG_TDLEN);
    if(!ringbytes || (ringbytes%16U) ||
       ringbytes>NAS_DMA_PAGE_BYTES){f->malformed_tx++;return;}
    uint32_t count=ringbytes/16U;
    if(tail>=count){f->malformed_tx++;return;}
    uint64_t addr=dma_ring_addr(f,REG_TDBAL,REG_TDBAH);
    if(!addr || (addr&15U)){f->malformed_tx++;return;}
    uint8_t *descriptors=(uint8_t *)(uintptr_t)addr;
    uint32_t head=fake_reg(f,REG_TDH);
    for(unsigned n=0;n<count && head!=tail;n++) {
        uint8_t *d=descriptors+head*16U;
        uint64_t data_addr=get64(d);
        uint32_t bytes=get16(d+8U);
        const uint8_t cmd=d[11U];
        if(!data_addr || bytes<60U || bytes>1518U ||
           !(cmd&TX_EOP) || !(cmd&TX_IFCS)) {
            f->malformed_tx++;
        } else if(f->tx_count<MAX_FRAMES) {
            memcpy(f->tx[f->tx_count],
                   (const void *)(uintptr_t)data_addr,bytes);
            f->tx_bytes[f->tx_count]=bytes;
            f->tx_count++;
        }
        d[12U]|=TX_DD;
        head=(head+1U)%count;
    }
    f->r[REG_TDH/4U]=head;
    f->r[REG_ICR/4U]|=1U; /* TXDW */
}
static int fake_read(void *ctx,uint32_t offset,uint32_t *value) {
    struct fake_e1000 *f=ctx;
    if(!f || !value || (offset&3U) ||
       offset>=E1000_REG_BYTES)return -EIO;
    if(offset==REG_ICR) {
        *value=f->r[REG_ICR/4U];
        f->r[REG_ICR/4U]=0U;
        return 0;
    }
    if(offset==REG_STATUS) {
        *value=f->no_link ? 0U : f->r[offset/4U];
        return 0;
    }
    *value=f->r[offset/4U];
    return 0;
}
static int fake_write(void *ctx,uint32_t offset,uint32_t value) {
    struct fake_e1000 *f=ctx;
    if(!f || (offset&3U) || offset>=E1000_REG_BYTES)return -EIO;
    if(offset==REG_CTRL && (value&CTRL_RST)) {
        if(f->withhold_reset_completion) {
            f->r[offset/4U]=value;return 0;
        }
        f->r[offset/4U]=value&~CTRL_RST;
        /* A reset makes all engines and outstanding DMA inactive. */
        dma_live=false;
        return 0;
    }
    if(offset==REG_EERD) {
        const uint32_t word=(value>>8)&0xffU;
        const uint16_t macwords[3]={0x2212U,0x4433U,0x6655U};
        f->r[offset/4U]=(value&0xffffU)|
                          (f->stuck_eerd ? 0U : (1U<<4))|
             ((uint32_t)((!f->bad_eeprom && word<3U) ?
                               macwords[word] : 0U)<<16);
        return 0;
    }
    if(offset==REG_TCTL && (value&TCTL_EN) && f->fail_tctl_enable)
        return -EIO;
    if(offset==REG_IMC) {
        f->r[REG_IMS/4U]&=~value;
    } else if(offset==REG_IMS) {
        f->r[REG_IMS/4U]|=value;
    } else if(offset==REG_TDT) {
        f->r[offset/4U]=value;
        if(f->r[REG_TCTL/4U]&TCTL_EN)fake_tx_execute(f,value);
    } else {
        f->r[offset/4U]=value;
    }
    if((f->r[REG_RCTL/4U]&RCTL_EN) ||
       (f->r[REG_TCTL/4U]&TCTL_EN))dma_live=true;
    if(!(f->r[REG_RCTL/4U]&RCTL_EN) &&
       !(f->r[REG_TCTL/4U]&TCTL_EN))dma_live=false;
    return 0;
}
static void reset(struct fake_e1000 *f) {
    for(unsigned i=0;i<allocations;i++) {
        if(frames[i])free(frames[i]);
        frames[i]=NULL;
    }
    allocations=frees=0;
    dma_live=invalid_free=false;
    memset(f,0,sizeof(*f));
    f->r[REG_STATUS/4U]=(1U<<1); /* LU */
    f->r[REG_RAL0/4U]=UINT32_C(0x44332212);
    f->r[REG_RAH0/4U]=UINT32_C(0x80006655);
}
static int open_dev(struct fake_e1000 *f,struct nas_e1000_io *io,
                    struct nas_e1000_session *session) {
    *io=(struct nas_e1000_io){
        .context=f,.mmio_bytes=E1000_REG_BYTES,
        .read32=fake_read,.write32=fake_write
    };
    return nas_e1000_open(io,&nas_dma_qemu_coherent,session);
}
struct received {
    unsigned count;
    uint8_t frame[2048];
    uint32_t bytes;
};
static int on_frame(void *context,const uint8_t *frame,uint32_t bytes) {
    struct received *r=context;
    if(bytes>sizeof(r->frame))return -EINVAL;
    memcpy(r->frame,frame,bytes);
    r->bytes=bytes;
    ++r->count;
    return 0;
}
static int inject_rx(struct fake_e1000 *f,const uint8_t *data,
                     uint16_t bytes,uint8_t status,uint8_t errors) {
    uint32_t ringbytes=fake_reg(f,REG_RDLEN);
    if(!ringbytes || (ringbytes%16U))return -1;
    uint32_t count=ringbytes/16U;
    uint32_t head=fake_reg(f,REG_RDH);
    if(head>=count || !(fake_reg(f,REG_RCTL)&RCTL_EN))return -1;
    uint64_t addr=dma_ring_addr(f,REG_RDBAL,REG_RDBAH);
    if(!addr)return -1;
    uint8_t *d=(uint8_t *)(uintptr_t)(addr+head*16U);
    uint64_t buffer=get64(d);
    if(!buffer)return -1;
    memcpy((void *)(uintptr_t)buffer,data,bytes);
    put16(d+8U,bytes);
    d[12U]=status;
    d[13U]=errors;
    f->r[REG_RDH/4U]=(head+1U)%count;
    f->r[REG_ICR/4U]|=(1U<<7); /* RXT0 */
    ++f->rx_count;
    return 0;
}
static int test_good_path(void) {
    struct fake_e1000 f;
    reset(&f);
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    CHECK(open_dev(&f,&io,&s)==0);
    CHECK(memcmp(s.mac,"\x12\x22\x33\x44\x55\x66",6U)==0);
    CHECK(nas_e1000_link_up(&s));
    uint8_t data[64];
    for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i+1U);
    CHECK(nas_e1000_transmit(&s,data,sizeof(data))==0);
    CHECK(f.tx_count==1U);
    CHECK(f.tx_bytes[0]==sizeof(data));
    CHECK(memcmp(f.tx[0],data,sizeof(data))==0);
    CHECK(f.malformed_tx==0U);
    /* ARP and control packets may be smaller than Ethernet minimum.
     * Software must pad before giving a descriptor to the NIC. */
    CHECK(nas_e1000_transmit(&s,data,42U)==0);
    CHECK(f.tx_count==2U && f.tx_bytes[1]==60U);
    CHECK(memcmp(f.tx[1],data,42U)==0);
    for(unsigned i=42U;i<60U;i++)CHECK(f.tx[1][i]==0U);
    CHECK(inject_rx(&f,data,sizeof(data),RX_DD|RX_EOP,0U)==0);
    struct received rx={0};
    CHECK(nas_e1000_poll(&s,8U,on_frame,&rx)==1);
    CHECK(rx.count==1U && rx.bytes==sizeof(data));
    CHECK(memcmp(rx.frame,data,sizeof(data))==0);
    CHECK(nas_e1000_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_no_link_and_bad_args(void) {
    struct fake_e1000 f;
    reset(&f);
    f.no_link=true;
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    CHECK(open_dev(&f,&io,&s)==0);
    CHECK(!nas_e1000_link_up(&s));
    CHECK(nas_e1000_transmit(&s,NULL,64U)==-EINVAL);
    uint8_t bad[2048]={0};
    CHECK(nas_e1000_transmit(&s,bad,0U)<0);
    CHECK(nas_e1000_transmit(&s,bad,sizeof(bad))<0);
    CHECK(nas_e1000_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_corrupt_rx_and_timeout(void) {
    struct fake_e1000 f;
    reset(&f);
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    CHECK(open_dev(&f,&io,&s)==0);
    uint8_t data[64]={0};
    CHECK(inject_rx(&f,data,sizeof(data),RX_DD|RX_EOP,0x20U)==0);
    struct received rx={0};
    CHECK(nas_e1000_poll(&s,8U,on_frame,&rx)>=0);
    CHECK(rx.count==0U);
    f.withhold_tx_completion=true;
    int rc=nas_e1000_transmit(&s,data,sizeof(data));
    CHECK(rc==0 || rc==-EAGAIN || rc==-ETIMEDOUT);
    CHECK(nas_e1000_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_tx_backpressure_preserves_owned_frame(void) {
    struct fake_e1000 f;
    reset(&f);
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    CHECK(open_dev(&f,&io,&s)==0);
    uint8_t first[64],second[64];
    memset(first,0x31U,sizeof(first));
    memset(second,0x52U,sizeof(second));
    f.withhold_tx_completion=true;
    CHECK(nas_e1000_transmit(&s,first,sizeof(first))==0);
    CHECK(s.tx_pending && f.tx_count==0U);
    CHECK(nas_e1000_transmit(&s,second,sizeof(second))==-EAGAIN);
    CHECK(memcmp(s.tx_page.cpu,first,sizeof(first))==0);
    f.withhold_tx_completion=false;
    fake_tx_execute(&f,fake_reg(&f,REG_TDT));
    CHECK(f.tx_count==1U);
    CHECK(memcmp(f.tx[0],first,sizeof(first))==0);
    CHECK(nas_e1000_transmit(&s,second,sizeof(second))==0);
    CHECK(f.tx_count==2U);
    CHECK(memcmp(f.tx[1],second,sizeof(second))==0);
    CHECK(nas_e1000_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_close_timeout_keeps_dma_pinned(void) {
    struct fake_e1000 f;
    reset(&f);
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    CHECK(open_dev(&f,&io,&s)==0);
    CHECK(allocations==7U && frees==0U);
    f.withhold_reset_completion=true;
    CHECK(nas_e1000_close(&s)==-ETIMEDOUT);
    CHECK(allocations==7U && frees==0U && !invalid_free);
    CHECK(s.lease.may_dma);
    f.withhold_reset_completion=false;
    CHECK(nas_e1000_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_missing_mac_rejected_without_dma_leak(void) {
    struct fake_e1000 f;
    reset(&f);
    f.r[REG_RAH0/4U]&=~(1U<<31);
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    /* Valid 82540EM may need an EEPROM fallback to populate RAL/RAH. */
    CHECK(open_dev(&f,&io,&s)==0);
    CHECK(memcmp(s.mac,"\x12\x22\x33\x44\x55\x66",6U)==0);
    CHECK(nas_e1000_close(&s)==0);
    CHECK(allocations==frees && !invalid_free);
    reset(&f);
    f.bad_eeprom=true;
    f.r[REG_RAH0/4U]&=~(1U<<31);
    CHECK(open_dev(&f,&io,&s)==-EOPNOTSUPP);
    CHECK(allocations==frees && !invalid_free);
    reset(&f);
    f.bad_eeprom=true;
    f.r[REG_RAL0/4U]|=1U; /* multicast source MAC is not an E1000 NIC */
    CHECK(open_dev(&f,&io,&s)==-EOPNOTSUPP);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_unique_q35_pci_candidate_gate(void) {
    struct nas_pci_function_info fn={
        .bus=0U,.slot=4U,.function=0U,
        .vendor_id=0x8086U,.device_id=0x100eU,
        .class_code=2U,.subclass=0U
    };
    struct nas_pci_resources r={.bar_count=1U};
    r.bars[0]=(struct nas_pci_bar_desc){
        .kind=NAS_PCI_BAR_MEM32,.index=0U,
        .prefetchable=false,.base=UINT64_C(0x810a0000)
    };
    CHECK(nas_x86_h10_e1000_candidate(&fn,&r));
    CHECK(!nas_x86_h10_e1000_candidate(NULL,&r));
    CHECK(!nas_x86_h10_e1000_candidate(&fn,NULL));
    r.bar_count=0U;
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bar_count=1U;
    r.bars[0].index=1U;
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bars[0].index=0U;
    r.bars[0].prefetchable=true;
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bars[0].prefetchable=false;
    r.bars[0].kind=NAS_PCI_BAR_MEM64;
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bars[0].kind=NAS_PCI_BAR_MEM32;
    r.bars[0].base=UINT64_C(0x810a0001);
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bars[0].base=UINT64_C(0x81fe1000);
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bars[0].base=UINT64_C(0x80ff0000);
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    r.bars[0].base=UINT64_C(0x810a0000);
    fn.device_id=0x10d3U; /* QEMU e1000e is a different NIC. */
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    fn.device_id=0x100eU;
    fn.slot=5U;
    CHECK(!nas_x86_h10_e1000_candidate(&fn,&r));
    fn.slot=4U;
    CHECK(nas_x86_h10_e1000_candidate(&fn,&r));
    /* Presence, absence and ambiguous *counts* are intentionally distinct:
     * the PCI walker binds only when exactly one candidate has completed
     * read-only discovery, never just because a match exists. */
    struct nas_pci_function_info matches[2]={fn,fn};
    unsigned candidates=0;
    for(unsigned i=0;i<2U;i++)
        candidates+=nas_x86_h10_e1000_candidate(&matches[i],&r)?1U:0U;
    CHECK(candidates==2U);
    CHECK(candidates!=1U);
    return 0;
}
static int test_reset_timeout(void) {
    struct fake_e1000 f;
    reset(&f);
    f.withhold_reset_completion=true;
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    int rc=open_dev(&f,&io,&s);
    CHECK(rc<0);
    CHECK(allocations==frees && !invalid_free);
    return 0;
}
static int test_eerd_timeout_and_partial_enable_rollback(void) {
    struct fake_e1000 f;
    reset(&f);
    f.r[REG_RAH0/4U]&=~(1U<<31);
    f.stuck_eerd=true;
    struct nas_e1000_io io={0};
    struct nas_e1000_session s={0};
    CHECK(open_dev(&f,&io,&s)==-ETIMEDOUT);
    CHECK(allocations==frees && !invalid_free);
    reset(&f);
    f.fail_tctl_enable=true;
    CHECK(open_dev(&f,&io,&s)==-EIO);
    CHECK(allocations==7U && frees==7U && !invalid_free);
    return 0;
}
int main(void) {
    if(test_good_path() || test_no_link_and_bad_args() ||
       test_corrupt_rx_and_timeout() ||
       test_tx_backpressure_preserves_owned_frame() ||
       test_close_timeout_keeps_dma_pinned() ||
       test_missing_mac_rejected_without_dma_leak() ||
       test_unique_q35_pci_candidate_gate() ||
       test_reset_timeout() ||
       test_eerd_timeout_and_partial_enable_rollback())
        return 1;
    puts("[H10 E1000 HOST PASS] TX/RX legacy descriptors and short-frame padding, backpressure, MAC EEPROM fallback/fail-closed, Q35 BAR identity, invalid RX, reset timeout DMA pin/retry; QEMU still required");
    return 0;
}
