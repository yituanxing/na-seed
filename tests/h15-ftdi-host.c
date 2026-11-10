/* H15 FT232BM USB serial xHCI host adversary.
 *
 * Synthetic xHC commands/USB descriptor responses are HOST TESTS ONLY.
 * This is not evidence of genuine QEMU USB data nor original Alpine programs.
 * Positive completion requires the independent H15 QEMU chardev nonce fixture.
 */
#include "../kernel/drivers/usb/xhci.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"[H15 FTDI HOST FAIL] line %u: %s\n", \
            (unsigned)__LINE__,#x); return 1; \
} } while (0)
#define PAGE 4096U
#define MMIO 0x4000U
#define USBCMD 0x20U
#define USBSTS 0x24U
#define PORTSC 0x420U
#define DB0 0x1000U
#define DB1 0x1004U
#define EVENT_TRANSFER 32U
#define EVENT_COMMAND 33U
#define EVENT_PORT 34U
#define CC_SUCCESS 1U
#define CC_SHORT 13U

struct test_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed,aligned(16)));

struct model {
    uint32_t reg[MMIO/4U];
    struct nas_xhci_ro_session *owner;
    unsigned allocated,freed,freed_while_running;
    unsigned invalid_mmio, bad_usb, commands,control_calls;
    unsigned vendor_reset, vendor_set_data, vendor_set_baud;
    unsigned ep0_cursor,command_cursor,event_producer,out_cursor;
    unsigned rx_armed,tx_transfers,tx_bytes;
    uint8_t tx_record[512];
    bool stop_fail,port_missing;
    bool bad_vid,bad_pid,bad_iface,bad_in_ep,bad_out_ep,bad_packet;
};

struct fixture {
    struct model m;
    struct nas_xhci_io io;
    struct nas_dma_provider dma;
    struct nas_xhci_ro_session s;
};

uint64_t nas_frame_alloc(void) { return 0; }
uint64_t nas_hhdm_offset(void) { return 0; }
bool nas_frame_put(uint64_t frame) { (void)frame;return false; }

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static int alloc_page(void *context,struct nas_dma_page *page) {
    struct model *m=context;
    void *p=aligned_alloc(PAGE,PAGE);
    if(!p)return -ENOMEM;
    memset(p,0,PAGE);
    page->cpu=p;
    page->bytes=PAGE;
    page->frame_pa=(uint64_t)(uintptr_t)p;
    page->bus_address=(uint64_t)(uintptr_t)p;
    ++m->allocated;
    return 0;
}
static int release_page(void *context,struct nas_dma_page *page) {
    struct model *m=context;
    if(!(m->reg[USBSTS/4U]&1U))++m->freed_while_running;
    free(page->cpu);
    ++m->freed;
    return 0;
}
static int read32(void *context,uint32_t off,uint32_t *value) {
    struct model *m=context;
    if(!value || (off&3U) || off>=MMIO) {
        ++m->invalid_mmio;return -EINVAL;
    }
    *value=m->reg[off/4U];
    if(off==PORTSC && m->port_missing)*value&=~1U;
    return 0;
}
static int push_event(struct model *m,uint32_t type,uint32_t code,
                      uint32_t residual,uint64_t ptr,uint8_t slot,uint8_t dci) {
    struct nas_xhci_ro_session *s=m->owner;
    if(!s || !s->pages[2].cpu || m->event_producer>=256U)
        return -EOVERFLOW;
    struct test_trb *events=s->pages[2].cpu;
    struct test_trb *trb=&events[m->event_producer++];
    trb->parameter=ptr;
    trb->status=(code<<24)|(residual&0x00ffffffU);
    trb->control=(type<<10)|((uint32_t)dci<<16)|
                 ((uint32_t)slot<<24)|1U;
    return 0;
}
static void fill_descriptors(struct model *m,
                             uint8_t device[18],uint8_t cfg[32]) {
    static const uint8_t good_device[18] = {
        18,1,0,2,0,0,0,8, 3,4,1,0x60,0,4,1,2,3,1
    };
    static const uint8_t good_config[32] = {
        9,2,32,0,1,1,0,0xa0,50,
        9,4,0,0,2,0xff,0xff,0xff,0,
        7,5,0x81,2,64,0,0,
        7,5,0x02,2,64,0,0
    };
    memcpy(device,good_device,18U);
    memcpy(cfg,good_config,32U);
    if(m->bad_vid)device[8]=0;
    if(m->bad_pid)device[10]=0;
    if(m->bad_iface)cfg[14]=0x08;
    if(m->bad_in_ep)cfg[20]=0x82;
    if(m->bad_out_ep)cfg[27]=0x03;
    if(m->bad_packet)cfg[22]=0x08;
}
static int control_done(struct model *m) {
    struct nas_xhci_ro_session *s=m->owner;
    struct test_trb *r=s->pages[5].cpu;
    unsigned setup_idx=m->ep0_cursor++;
    const struct test_trb *setup=&r[setup_idx];
    if(((setup->control>>10)&63U)!=2U)return -EPROTO;
    uint8_t request[8];
    memcpy(request,&setup->parameter,8U);
    uint16_t value=le16(request+2U);
    uint16_t length=le16(request+6U);
    struct test_trb *data=NULL;
    if(length) {
        data=&r[m->ep0_cursor++];
        if(((data->control>>10)&63U)!=3U)return -EPROTO;
    }
    unsigned status_idx=m->ep0_cursor++;
    if(((r[status_idx].control>>10)&63U)!=4U)return -EPROTO;
    ++m->control_calls;
    if(request[0]==0x80U && request[1]==6U &&
       ((value>>8)==1U || (value>>8)==2U) && data) {
        uint8_t device[18],cfg[32];
        fill_descriptors(m,device,cfg);
        const uint8_t *src=(value>>8)==1U?device:cfg;
        size_t limit=(value>>8)==1U?18U:32U;
        if(length>limit || data->status!=length)return -EPROTO;
        memcpy((void *)(uintptr_t)data->parameter,src,length);
    } else if(request[0]==0U && request[1]==9U && value==1U &&
              length==0U) {
        /* USB SET_CONFIGURATION(1). */
    } else if(request[0]==0x40U && length==0U) {
        if(request[1]==0U)++m->vendor_reset;
        else if(request[1]==4U)++m->vendor_set_data;
        else if(request[1]==3U)++m->vendor_set_baud;
        else if(request[1]!=1U && request[1]!=2U &&
                request[1]!=9U && request[1]!=6U &&
                request[1]!=7U)return -EPROTO;
    } else if(request[0]==0xc0U && request[1]==5U &&
              length==2U && data) {
        const uint8_t modem[2]={0x31U,0x60U};
        memcpy((void *)(uintptr_t)data->parameter,modem,2U);
    } else {
        ++m->bad_usb;return -EPROTO;
    }
    return push_event(m,EVENT_TRANSFER,CC_SUCCESS,0U,
                      s->pages[5].bus_address+status_idx*16U,
                      s->slot_id,1U);
}
static int command_done(struct model *m) {
    struct nas_xhci_ro_session *s=m->owner;
    struct test_trb *cmds=s->pages[1].cpu;
    unsigned idx=m->command_cursor++;
    struct test_trb *trb=&cmds[idx];
    unsigned type=(trb->control>>10)&63U;
    uint8_t slot=(uint8_t)(trb->control>>24);
    if(type==9U)slot=1U; /* ENABLE_SLOT */
    else if(type!=11U && type!=12U)return -EPROTO;
    ++m->commands;
    return push_event(m,EVENT_COMMAND,CC_SUCCESS,0U,
                      s->pages[1].bus_address+idx*16U,slot,0U);
}
static int out_done(struct model *m,uint32_t dci) {
    struct nas_xhci_ro_session *s=m->owner;
    struct test_trb *out=(struct test_trb *)
                          ((uint8_t *)s->pages[5].cpu+1024U);
    unsigned idx=m->out_cursor++;
    struct test_trb *trb=&out[idx];
    uint32_t bytes=trb->status&0x1ffffU;
    if(dci!=4U || ((trb->control>>10)&63U)!=1U ||
       !bytes || bytes>64U ||
       m->tx_bytes+bytes>sizeof(m->tx_record))
        return -EPROTO;
    memcpy(m->tx_record+m->tx_bytes,
           (void *)(uintptr_t)trb->parameter,bytes);
    m->tx_bytes+=bytes;
    ++m->tx_transfers;
    return push_event(m,EVENT_TRANSFER,CC_SUCCESS,0U,
                      s->pages[5].bus_address+1024U+idx*16U,
                      s->slot_id,(uint8_t)dci);
}
static int write32(void *context,uint32_t off,uint32_t value) {
    struct model *m=context;
    if((off&3U) || off>=MMIO) {
        ++m->invalid_mmio;return -EINVAL;
    }
    if(off==USBCMD) {
        if(value&2U) {
            m->reg[USBSTS/4U]|=1U;
            m->reg[USBCMD/4U]=0U;
            return 0;
        }
        if(value&1U)m->reg[USBSTS/4U]&=~1U;
        else if(!m->stop_fail)m->reg[USBSTS/4U]|=1U;
    }
    if(off==PORTSC && (value&(1U<<4))) {
        m->reg[PORTSC/4U]=(1U<<0)|(1U<<1)|
                          (1U<<9)|(1U<<10);
        return 0;
    }
    if(off==DB0)return command_done(m);
    if(off==DB1) {
        if(value==1U)return control_done(m);
        if(value==3U) {++m->rx_armed;return 0;}
        if(value==4U)return out_done(m,value);
        return -EPROTO;
    }
    m->reg[off/4U]=value;
    return 0;
}
static void fixture_init(struct fixture *f) {
    memset(f,0,sizeof(*f));
    f->m.owner=&f->s;
    f->io=(struct nas_xhci_io){
        .context=&f->m,.mmio_bytes=MMIO,
        .read32=read32,.write32=write32
    };
    f->dma=(struct nas_dma_provider){
        .context=&f->m,.coherent=true,
        .alloc_page=alloc_page,.release_page=release_page
    };
    f->m.reg[0U/4U]=0x20U;          /* CAPLENGTH */
    f->m.reg[4U/4U]=(1U<<24)|4U;   /* 1 root port, 4 slots */
    f->m.reg[0x10U/4U]=1U;         /* 64-bit addressing */
    f->m.reg[0x14U/4U]=0x1000U;    /* DBOFF */
    f->m.reg[0x18U/4U]=0x2000U;    /* RTSOFF */
    f->m.reg[(USBCMD+8U)/4U]=1U;   /* PAGESIZE=4KiB */
    f->m.reg[USBSTS/4U]=1U;        /* HCHalted */
    f->m.reg[PORTSC/4U]=1U|(1U<<10); /* FS connected before reset */
}
static int open_ready(struct fixture *f) {
    CHECK(nas_xhci_ftdi_open(&f->io,&f->dma,&f->s)==0);
    CHECK(f->s.ftdi_mode && f->s.ready && f->s.ftdi_pending);
    CHECK(f->s.slot_id==1U && f->s.port==1U);
    CHECK(f->s.port_speed==1U);
    CHECK(f->s.bulk_in_dci==3U && f->s.bulk_out_dci==4U);
    CHECK(f->m.allocated==NAS_XHCI_RO_DMA_PAGES);
    CHECK(f->m.rx_armed==1U);
    CHECK(f->m.commands>=3U && f->m.control_calls>=4U);
    CHECK(f->m.bad_usb==0U && f->m.invalid_mmio==0U);
    return 0;
}
static int close_clean(struct fixture *f) {
    CHECK(nas_xhci_ftdi_close(&f->s)==0);
    CHECK(f->m.allocated==NAS_XHCI_RO_DMA_PAGES);
    CHECK(f->m.freed==f->m.allocated);
    CHECK(f->m.freed_while_running==0U);
    CHECK(f->m.invalid_mmio==0U);
    return 0;
}
static int rx_event(struct fixture *f,const uint8_t *wire,
                    uint32_t actual,uint32_t code,uint8_t slot,
                    uint8_t dci,uint64_t trb_ptr) {
    if(actual>64U)return -EINVAL;
    memset((uint8_t *)f->s.pages[6].cpu+128U,0xa6,64U);
    if(wire && actual)memcpy((uint8_t *)f->s.pages[6].cpu+128U,
                            wire,actual);
    return push_event(&f->m,EVENT_TRANSFER,code,64U-actual,
                      trb_ptr,slot,dci);
}
static int test_descriptor_rejection(void) {
    struct fixture f;
    for(unsigned which=0;which<7U;++which) {
        fixture_init(&f);
        if(which==0U)f.m.port_missing=true;
        if(which==1U)f.m.bad_vid=true;
        if(which==2U)f.m.bad_pid=true;
        if(which==3U)f.m.bad_iface=true;
        if(which==4U)f.m.bad_in_ep=true;
        if(which==5U)f.m.bad_out_ep=true;
        if(which==6U)f.m.bad_packet=true;
        int rc=nas_xhci_ftdi_open(&f.io,&f.dma,&f.s);
        CHECK(rc<0);
        CHECK(!f.s.ready);
        CHECK(nas_xhci_ftdi_close(&f.s)==0);
        CHECK(f.m.freed==f.m.allocated);
        CHECK(!f.m.freed_while_running && !f.m.invalid_mmio);
    }
    return 0;
}
static int test_rx_no_phantom_and_status_stripping(void) {
    struct fixture f;fixture_init(&f);
    CHECK(open_ready(&f)==0);
    uint8_t data[64];memset(data,0xb5,sizeof(data));
    uint32_t got=777U;
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,62U,&got)==-EAGAIN);
    CHECK(got==0U && data[0]==0xb5U);
    CHECK(f.m.rx_armed==1U);

    const uint8_t status_only[2]={0xb1U,0x00U};
    CHECK(rx_event(&f,status_only,2U,CC_SHORT,1U,3U,
                   f.s.ftdi_in_trb_bus)==0);
    got=999U;
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,62U,&got)==-EAGAIN);
    CHECK(got==0U && data[0]==0xb5U);
    CHECK(f.m.rx_armed>=2U); /* status-only packet must rearm */

    const uint8_t first_wire[9]={0xb1U,0x00U,
        'F','T','D','I','1','2','3'};
    CHECK(rx_event(&f,first_wire,sizeof(first_wire),CC_SHORT,
                   1U,3U,f.s.ftdi_in_trb_bus)==0);
    got=999U;
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,62U,&got)==0);
    CHECK(got==7U);
    CHECK(memcmp(data,"FTDI123",7U)==0);
    CHECK(data[7]==0xb5U);
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,62U,&got)==-EAGAIN);
    CHECK(got==0U);
    CHECK(close_clean(&f)==0);
    return 0;
}
static int test_rx_split_read_and_full_packet(void) {
    struct fixture f;fixture_init(&f);
    CHECK(open_ready(&f)==0);
    uint8_t wire[64],data[64];
    wire[0]=0xb1U;wire[1]=0x00U;
    for(unsigned i=2U;i<64U;++i)wire[i]=(uint8_t)(i+7U);
    CHECK(rx_event(&f,wire,64U,CC_SUCCESS,1U,3U,
                   f.s.ftdi_in_trb_bus)==0);
    uint32_t got=0U;
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,9U,&got)==0);
    CHECK(got==9U && memcmp(data,wire+2U,9U)==0);
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,53U,&got)==0);
    CHECK(got==53U && memcmp(data,wire+11U,53U)==0);
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,53U,&got)==-EAGAIN);
    CHECK(got==0U);
    CHECK(close_clean(&f)==0);
    return 0;
}
static int test_tx_large_stream_in_legal_usb_chunks(void) {
    struct fixture f;fixture_init(&f);
    CHECK(open_ready(&f)==0);
    uint8_t source[137];
    for(unsigned i=0;i<sizeof(source);++i)
        source[i]=(uint8_t)(i*37U+11U);
    uint32_t off=0U;
    while(off<sizeof(source)) {
        uint32_t n=(uint32_t)(sizeof(source)-off);
        if(n>64U)n=64U;
        CHECK(nas_xhci_ftdi_write(&f.s,source+off,n)==0);
        off+=n;
    }
    CHECK(f.m.tx_transfers==3U);
    CHECK(f.m.tx_bytes==sizeof(source));
    CHECK(memcmp(f.m.tx_record,source,sizeof(source))==0);
    CHECK(nas_xhci_ftdi_write(&f.s,source,65U)<0);
    CHECK(f.m.tx_transfers==3U);
    CHECK(close_clean(&f)==0);
    return 0;
}
static int test_rx_wrong_event_fails_closed(void) {
    struct fixture f;
    uint8_t data[62]={0};
    const uint8_t wire[4]={0xb1U,0x00U,'A','B'};
    for(unsigned which=0;which<5U;++which) {
        fixture_init(&f);
        CHECK(open_ready(&f)==0);
        uint64_t ptr=f.s.ftdi_in_trb_bus;
        uint8_t slot=1U,dci=3U;
        uint32_t completion=CC_SHORT;
        if(which==0U)ptr+=16U;
        if(which==1U)slot=2U;
        if(which==2U)dci=4U;
        if(which==3U)completion=6U; /* STALL */
        if(which==4U) {
            CHECK(push_event(&f.m,EVENT_COMMAND,CC_SUCCESS,0U,
                             ptr,slot,dci)==0);
        } else
            CHECK(rx_event(&f,wire,sizeof(wire),completion,
                           slot,dci,ptr)==0);
        uint32_t got=100U;
        CHECK(nas_xhci_ftdi_poll_in(&f.s,data,sizeof(data),&got)<0);
        CHECK(got==0U);
        CHECK(close_clean(&f)==0);
    }
    return 0;
}
static int test_port_disconnect(void) {
    struct fixture f;fixture_init(&f);
    CHECK(open_ready(&f)==0);
    f.m.port_missing=true;
    CHECK(push_event(&f.m,EVENT_PORT,CC_SUCCESS,0U,
                     0U,0U,0U)==0);
    uint8_t data[62]={0};uint32_t got=42U;
    CHECK(nas_xhci_ftdi_poll_in(&f.s,data,62U,&got)==-ENODEV);
    CHECK(got==0U);
    CHECK(close_clean(&f)==0);
    return 0;
}
static int test_halted_gate_preserves_all_seven_pages(void) {
    struct fixture f;fixture_init(&f);
    CHECK(open_ready(&f)==0);
    f.m.stop_fail=true;
    CHECK(nas_xhci_ftdi_close(&f.s)<0);
    CHECK(f.m.freed==0U);
    CHECK(f.s.may_dma && f.s.lease.may_dma);
    f.m.stop_fail=false;
    CHECK(close_clean(&f)==0);
    return 0;
}
int main(void) {
    CHECK(test_descriptor_rejection()==0);
    CHECK(test_rx_no_phantom_and_status_stripping()==0);
    CHECK(test_rx_split_read_and_full_packet()==0);
    CHECK(test_tx_large_stream_in_legal_usb_chunks()==0);
    CHECK(test_rx_wrong_event_fails_closed()==0);
    CHECK(test_port_disconnect()==0);
    CHECK(test_halted_gate_preserves_all_seven_pages()==0);
    puts("[H15 FTDI HOST PASS] vendor ID/interface/endpoint negatives, 2-byte status strip, 0/short/full packets, split reads, 137B in 64B USB writes, foreign TRB/slot/DCI/completion, disconnect and HCHalted-gated 7 DMA pages; genuine QEMU+signed Alpine required");
    return 0;
}
