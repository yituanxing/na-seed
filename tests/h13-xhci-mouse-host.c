/* H13 isolated xHCI Boot Mouse TRB adversary. Host-synthetic completions
 * exercise production xhci.c and DMA lease, never claim real QEMU input. */
#include "../kernel/drivers/usb/xhci.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(p) do { if(!(p)) { fprintf(stderr, \
    "[H13 XHCI HOST FAIL] line %u: %s\n",__LINE__,#p);return 1; } } while(0)
#define PAGE 4096U
#define MMIO 0x4000U
#define USBCMD 0x20U
#define USBSTS 0x24U
#define PORTSC 0x420U

struct model {
    uint32_t reg[MMIO / 4U];
    unsigned allocated, freed, doorbells, bad_mmio;
    bool stop_fail;
};
uint64_t nas_frame_alloc(void) { return 0; }
uint64_t nas_hhdm_offset(void) { return 0; }
bool nas_frame_put(uint64_t frame) { (void)frame; return false; }

static int page_alloc(void *context,struct nas_dma_page *p) {
    struct model *m=context;
    void *mem=aligned_alloc(PAGE,PAGE);
    if(!mem)return -ENOMEM;
    memset(mem,0,PAGE);
    p->cpu=mem;p->bytes=PAGE;
    p->frame_pa=(uint64_t)(uintptr_t)mem;
    p->bus_address=(uint64_t)(uintptr_t)mem;
    ++m->allocated;
    return 0;
}
static int page_free(void *context,struct nas_dma_page *p) {
    struct model *m=context;free(p->cpu);++m->freed;return 0;
}
static int rd(void *context,uint32_t offset,uint32_t *out) {
    struct model *m=context;
    if(!out || (offset&3U) || offset>=MMIO) {
        ++m->bad_mmio;return -EINVAL;
    }
    *out=m->reg[offset/4U];return 0;
}
static int wr(void *context,uint32_t offset,uint32_t value) {
    struct model *m=context;
    if((offset&3U) || offset>=MMIO) {
        ++m->bad_mmio;return -EINVAL;
    }
    if(offset==USBCMD && !(value&1U) && !m->stop_fail)
        m->reg[USBSTS/4U]|=1U;
    if(offset==0x1004U) {
        if(value!=3U)return -EIO;
        ++m->doorbells;
    }
    m->reg[offset/4U]=value;return 0;
}

struct fixture {
    struct model m;
    struct nas_xhci_io io;
    struct nas_dma_provider dma;
    struct nas_xhci_ro_session s;
};
static int setup(struct fixture *f) {
    memset(f,0,sizeof(*f));
    f->io=(struct nas_xhci_io){.context=&f->m,.mmio_bytes=MMIO,
        .read32=rd,.write32=wr};
    f->dma=(struct nas_dma_provider){.context=&f->m,.coherent=true,
        .alloc_page=page_alloc,.release_page=page_free};
    struct nas_xhci_ro_session *s=&f->s;
    s->io=&f->io;s->dma=&f->dma;
    CHECK(nas_dma_lease_begin(&s->lease,&f->dma)==0);
    for(unsigned i=0;i<NAS_XHCI_RO_DMA_PAGES;i++)
        CHECK(nas_dma_lease_alloc_page(&s->lease,&s->pages[i])==0);
    CHECK(nas_dma_lease_arm(&s->lease)==0);
    s->may_dma=true;s->ready=true;s->hid_mode=true;
    s->hid_protocol=2U;s->hid_report_bytes=4U;
    s->hid_pending=true;s->hid_packet=4U;
    s->slot_id=1;s->hid_in_dci=3U;s->ep_cycle[1]=1U;
    s->event_cycle=1U;s->op_base=0x20U;s->rt_base=0x2000U;
    s->db_base=0x1000U;s->portsc_offset=PORTSC;
    s->hid_trb_bus=s->pages[5].bus_address+1024U;
    f->m.reg[USBCMD/4U]=1U;
    f->m.reg[PORTSC/4U]=3U;
    return 0;
}
static void complete(struct fixture *f,uint32_t kind,uint32_t cc,
                     uint32_t residual,uint64_t trb,uint8_t dci) {
    struct nas_xhci_ro_session *s=&f->s;
    uint8_t *p=(uint8_t*)s->pages[2].cpu+s->event_pos*16U;
    uint32_t status=(cc<<24)|residual;
    uint32_t control=(kind<<10)|((uint32_t)dci<<16)|
                      ((uint32_t)s->slot_id<<24)|s->event_cycle;
    memcpy(p,&trb,8);memcpy(p+8,&status,4);memcpy(p+12,&control,4);
}
static int cleanup(struct fixture *f) {
    CHECK(nas_xhci_hid_close(&f->s)==-EINVAL);
    CHECK(nas_xhci_hid_mouse_close(&f->s)==0);
    CHECK(f->m.allocated==NAS_XHCI_RO_DMA_PAGES);
    CHECK(f->m.freed==NAS_XHCI_RO_DMA_PAGES);
    CHECK(!f->m.bad_mmio);
    return 0;
}
static int test_mouse_exact4_and_rearm(void) {
    struct fixture f;
    CHECK(setup(&f)==0);
    uint8_t bytes[4]={0xa5,0xa5,0xa5,0xa5};
    uint8_t keyboard[8]={0};
    CHECK(nas_xhci_hid_poll(&f.s,keyboard)==-EINVAL);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,bytes)==-EAGAIN);
    CHECK(f.m.doorbells==0);
    const uint8_t report[4]={1U,20U,(uint8_t)-7,1U};
    memcpy(f.s.pages[6].cpu,report,4);
    complete(&f,32U,1U,0U,f.s.hid_trb_bus,3U);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,bytes)==0);
    CHECK(memcmp(bytes,report,4)==0);
    CHECK(f.m.doorbells==1);
    CHECK(f.s.hid_pending && f.s.ep_pos[1]==1U);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,bytes)==-EAGAIN);
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_mouse_short_and_bad_completion(void) {
    struct fixture f;
    uint8_t report[4];
    CHECK(setup(&f)==0);
    memset(report,0x55,4);
    complete(&f,32U,13U,1U,f.s.hid_trb_bus,3U);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,report)==-EMSGSIZE);
    CHECK(!f.s.hid_pending && f.m.doorbells==0);
    for(unsigned i=0;i<4;i++)CHECK(report[i]==0x55);
    CHECK(cleanup(&f)==0);

    CHECK(setup(&f)==0);
    complete(&f,32U,1U,0U,f.s.hid_trb_bus+16U,3U);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,report)==-EIO);
    CHECK(cleanup(&f)==0);

    CHECK(setup(&f)==0);
    complete(&f,32U,1U,0U,f.s.hid_trb_bus,2U);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,report)==-EIO);
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_disconnect_and_fail_closed_dma(void) {
    struct fixture f;
    uint8_t report[4]={0};
    CHECK(setup(&f)==0);
    f.m.reg[PORTSC/4U]=0;
    complete(&f,34U,1U,0U,0U,0U);
    CHECK(nas_xhci_hid_mouse_poll(&f.s,report)==-ENODEV);
    f.m.stop_fail=true;
    CHECK(nas_xhci_hid_mouse_close(&f.s)==-ETIMEDOUT);
    CHECK(f.m.freed==0 && f.s.may_dma && f.s.lease.may_dma);
    f.m.stop_fail=false;
    CHECK(cleanup(&f)==0);
    return 0;
}
int main(void) {
    CHECK(test_mouse_exact4_and_rearm()==0);
    CHECK(test_mouse_short_and_bad_completion()==0);
    CHECK(test_disconnect_and_fail_closed_dma()==0);
    puts("[H13 XHCI HOST PASS] exact mouse4B, keyboard-mode isolation, nonblocking/rearm, short/foreign TRB, disconnect, unhalted DMA pinning; real QEMU required");
    return 0;
}
