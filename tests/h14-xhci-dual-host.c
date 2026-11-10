/* H14 one-xHC/two-root-port HID adversarial HOST MODEL.
 * Synthetic TRBs validate production xhci.c demultiplexing and H6 leases,
 * never count as QEMU hardware or original Alpine runtime acceptance.
 */
#include "../kernel/drivers/usb/xhci.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr,"[H14 DUAL XHCI HOST FAIL] line %u: %s\n", \
            (unsigned)__LINE__,#cond); return 1; \
} } while (0)
#define PAGE 4096U
#define MMIO 0x4000U
#define USBCMD 0x20U
#define USBSTS 0x24U
#define KBD_PORTSC 0x420U
#define MOUSE_PORTSC 0x430U
#define EVENT_TRANSFER 32U
#define EVENT_PORT 34U

struct model {
    uint32_t reg[MMIO/4U];
    unsigned allocated, freed, doorbell_kbd, doorbell_mouse, bad_mmio;
    bool fail_stop;
};

uint64_t nas_frame_alloc(void) { return 0; }
uint64_t nas_hhdm_offset(void) { return 0; }
bool nas_frame_put(uint64_t frame) { (void)frame; return false; }

static int alloc_page(void *context,struct nas_dma_page *p) {
    struct model *m=context;
    void *mem=aligned_alloc(PAGE,PAGE);
    if (!mem) return -ENOMEM;
    memset(mem,0,PAGE);
    p->cpu=mem;
    p->bytes=PAGE;
    p->frame_pa=(uint64_t)(uintptr_t)mem;
    p->bus_address=(uint64_t)(uintptr_t)mem;
    ++m->allocated;
    return 0;
}
static int release_page(void *context,struct nas_dma_page *p) {
    struct model *m=context;
    free(p->cpu);
    ++m->freed;
    return 0;
}
static int read32(void *context,uint32_t offset,uint32_t *out) {
    struct model *m=context;
    if (!out || (offset&3U) || offset>=MMIO) {
        ++m->bad_mmio;
        return -EINVAL;
    }
    *out=m->reg[offset/4U];
    return 0;
}
static int write32(void *context,uint32_t offset,uint32_t value) {
    struct model *m=context;
    if ((offset&3U) || offset>=MMIO) {
        ++m->bad_mmio;
        return -EINVAL;
    }
    if (offset==USBCMD && !(value&1U) && !m->fail_stop)
        m->reg[USBSTS/4U]|=1U;
    if (offset==0x1004U) {
        if (value!=3U) return -EIO;
        ++m->doorbell_kbd;
    } else if (offset==0x1008U) {
        if (value!=3U) return -EIO;
        ++m->doorbell_mouse;
    }
    m->reg[offset/4U]=value;
    return 0;
}

struct fixture {
    struct model m;
    struct nas_xhci_io io;
    struct nas_dma_provider dma;
    struct nas_xhci_ro_session s;
};

static void init_io(struct fixture *f) {
    memset(f,0,sizeof(*f));
    f->io=(struct nas_xhci_io){
        .context=&f->m,.mmio_bytes=MMIO,.read32=read32,.write32=write32
    };
    f->dma=(struct nas_dma_provider){
        .context=&f->m,.coherent=true,.alloc_page=alloc_page,
        .release_page=release_page
    };
}
static int init_active(struct fixture *f) {
    init_io(f);
    struct nas_xhci_ro_session *s=&f->s;
    s->io=&f->io;
    s->dma=&f->dma;
    CHECK(nas_dma_lease_begin(&s->lease,&f->dma)==0);
    for (unsigned i=0;i<NAS_XHCI_RO_DMA_PAGES;++i)
        CHECK(nas_dma_lease_alloc_page(&s->lease,&s->pages[i])==0);
    CHECK(nas_dma_lease_begin(&s->dual_extra_lease,&f->dma)==0);
    for (unsigned i=0;i<NAS_XHCI_DUAL_EXTRA_PAGES;++i)
        CHECK(nas_dma_lease_alloc_page(&s->dual_extra_lease,
                                       &s->dual_extra_pages[i])==0);
    CHECK(nas_dma_lease_arm(&s->lease)==0);
    CHECK(nas_dma_lease_arm(&s->dual_extra_lease)==0);
    s->may_dma=true;
    s->ready=true;
    s->hid_mode=true;
    s->dual_mode=true;
    s->event_cycle=1U;
    s->op_base=0x20U;
    s->rt_base=0x2000U;
    s->db_base=0x1000U;
    s->dual_current=0U;

    struct nas_xhci_dual_hid_channel *k=&s->dual_channels[0];
    k->slot_id=1U;
    k->port=1U;
    k->port_speed=2U;
    k->portsc_offset=KBD_PORTSC;
    k->hid_protocol=1U;
    k->hid_report_bytes=8U;
    k->hid_packet=8U;
    k->hid_in_dci=3U;
    k->hid_interval=4U;
    k->hid_pending=true;
    k->hid_trb_bus=s->pages[5].bus_address+1024U;
    for (unsigned i=0;i<3U;++i)k->ep_cycle[i]=1U;

    struct nas_xhci_dual_hid_channel *m=&s->dual_channels[1];
    m->slot_id=2U;
    m->port=2U;
    m->port_speed=2U;
    m->portsc_offset=MOUSE_PORTSC;
    m->hid_protocol=2U;
    m->hid_report_bytes=4U;
    m->hid_packet=4U;
    m->hid_in_dci=3U;
    m->hid_interval=4U;
    m->hid_pending=true;
    m->hid_trb_bus=s->dual_extra_pages[1].bus_address+1024U;
    for (unsigned i=0;i<3U;++i)m->ep_cycle[i]=1U;

    /* Active legacy scratch mirrors keyboard channel zero. The driver must
     * preserve the other independent channel through every slot selection. */
    s->slot_id=k->slot_id;
    s->port=k->port;
    s->port_speed=k->port_speed;
    s->portsc_offset=k->portsc_offset;
    s->hid_protocol=k->hid_protocol;
    s->hid_report_bytes=k->hid_report_bytes;
    s->hid_packet=k->hid_packet;
    s->hid_in_dci=k->hid_in_dci;
    s->hid_interval=k->hid_interval;
    s->hid_pending=k->hid_pending;
    s->hid_trb_bus=k->hid_trb_bus;
    for (unsigned i=0;i<3U;++i)s->ep_cycle[i]=k->ep_cycle[i];
    f->m.reg[USBCMD/4U]=1U;
    f->m.reg[KBD_PORTSC/4U]=3U;
    f->m.reg[MOUSE_PORTSC/4U]=3U;
    return 0;
}
static void event(struct fixture *f,unsigned index,unsigned device,
                  uint32_t kind,uint32_t completion,uint32_t residual,
                  uint64_t ptr,uint8_t slot,uint8_t dci) {
    uint8_t *p=(uint8_t *)f->s.pages[2].cpu+16U*index;
    uint32_t status=(completion<<24)|residual;
    uint32_t ctl=(kind<<10)|((uint32_t)dci<<16)|
                 ((uint32_t)slot<<24)|f->s.event_cycle;
    (void)device;
    memcpy(p,&ptr,8U);
    memcpy(p+8U,&status,4U);
    memcpy(p+12U,&ctl,4U);
}
static void report_event(struct fixture *f,unsigned index,unsigned device) {
    struct nas_xhci_dual_hid_channel *c=&f->s.dual_channels[device];
    event(f,index,device,EVENT_TRANSFER,1U,0U,
          c->hid_trb_bus,c->slot_id,c->hid_in_dci);
}
static int cleanup(struct fixture *f) {
    CHECK(nas_xhci_ro_close(&f->s)==-EINVAL);
    CHECK(nas_xhci_hid_close(&f->s)==-EINVAL);
    CHECK(nas_xhci_hid_mouse_close(&f->s)==-EINVAL);
    CHECK(nas_xhci_dual_hid_close(&f->s)==0);
    CHECK(f->m.allocated==NAS_XHCI_RO_DMA_PAGES+
                          NAS_XHCI_DUAL_EXTRA_PAGES);
    CHECK(f->m.freed==f->m.allocated);
    CHECK(!f->m.bad_mmio);
    return 0;
}

static int test_interleaved_mouse_before_keyboard(void) {
    struct fixture f;
    CHECK(init_active(&f)==0);
    const uint8_t k[8]={0U,0U,4U,0U,0U,0U,0U,0U};
    const uint8_t m[4]={1U,20U,(uint8_t)-12,1U};
    memcpy(f.s.pages[6].cpu,k,sizeof(k));
    memcpy(f.s.dual_extra_pages[2].cpu,m,sizeof(m));
    report_event(&f,0,1U);
    report_event(&f,1,0U);
    uint8_t got_k[8]={0},got_m[4]={0};
    CHECK(nas_xhci_dual_hid_poll_keyboard(&f.s,got_k)==0);
    CHECK(memcmp(got_k,k,sizeof(k))==0);
    CHECK(f.s.dual_channels[1].complete_pending);
    CHECK(!f.m.doorbell_mouse); /* do not rearm unread off-target data */
    CHECK(f.m.doorbell_kbd==1U);
    CHECK(nas_xhci_dual_hid_poll_mouse(&f.s,got_m)==0);
    CHECK(memcmp(got_m,m,sizeof(m))==0);
    CHECK(f.m.doorbell_mouse==1U);
    CHECK(!f.s.dual_channels[1].complete_pending);
    CHECK(nas_xhci_dual_hid_poll_mouse(&f.s,got_m)==-EAGAIN);
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_interleaved_keyboard_before_mouse(void) {
    struct fixture f;
    CHECK(init_active(&f)==0);
    const uint8_t k[8]={0U,0U,5U,0U,0U,0U,0U,0U};
    const uint8_t m[4]={0U,(uint8_t)-3,10U,(uint8_t)-1};
    memcpy(f.s.pages[6].cpu,k,sizeof(k));
    memcpy(f.s.dual_extra_pages[2].cpu,m,sizeof(m));
    report_event(&f,0,0U);
    report_event(&f,1,1U);
    uint8_t got_k[8]={0},got_m[4]={0};
    CHECK(nas_xhci_dual_hid_poll_mouse(&f.s,got_m)==0);
    CHECK(memcmp(got_m,m,sizeof(m))==0);
    CHECK(f.s.dual_channels[0].complete_pending);
    CHECK(!f.m.doorbell_kbd);
    CHECK(f.m.doorbell_mouse==1U);
    CHECK(nas_xhci_dual_hid_poll_keyboard(&f.s,got_k)==0);
    CHECK(memcmp(got_k,k,sizeof(k))==0);
    CHECK(f.m.doorbell_kbd==1U);
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_unrelated_only_remains_cached(void) {
    struct fixture f;
    CHECK(init_active(&f)==0);
    const uint8_t k[8]={0U,0U,4U,0U,0U,0U,0U,0U};
    memcpy(f.s.pages[6].cpu,k,sizeof(k));
    report_event(&f,0,0U);
    uint8_t got_k[8]={0};
    uint8_t got_m[4]={0xa5,0xa5,0xa5,0xa5};
    CHECK(nas_xhci_dual_hid_poll_mouse(&f.s,got_m)==-EAGAIN);
    CHECK(f.s.dual_channels[0].complete_pending);
    CHECK(f.m.doorbell_kbd==0U);
    for (unsigned i=0;i<4U;++i) CHECK(got_m[i]==0xa5U);
    CHECK(nas_xhci_dual_hid_poll_keyboard(&f.s,got_k)==0);
    CHECK(memcmp(got_k,k,sizeof(k))==0);
    CHECK(f.m.doorbell_kbd==1U);
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_short_foreign_and_disconnect(void) {
    struct fixture f;
    uint8_t k[8];
    CHECK(init_active(&f)==0);
    memset(k,0x77U,sizeof(k));
    event(&f,0,0U,EVENT_TRANSFER,13U,1U,
          f.s.dual_channels[0].hid_trb_bus,1U,3U);
    CHECK(nas_xhci_dual_hid_poll_keyboard(&f.s,k)==-EMSGSIZE);
    CHECK(f.m.doorbell_kbd==0U);
    for(unsigned i=0;i<8U;++i)CHECK(k[i]==0x77U);
    CHECK(cleanup(&f)==0);

    CHECK(init_active(&f)==0);
    event(&f,0,0U,EVENT_TRANSFER,1U,0U,
          f.s.dual_channels[0].hid_trb_bus+16U,1U,3U);
    CHECK(nas_xhci_dual_hid_poll_keyboard(&f.s,k)==-EIO);
    CHECK(cleanup(&f)==0);

    CHECK(init_active(&f)==0);
    f.m.reg[MOUSE_PORTSC/4U]=0U;
    event(&f,0,0U,EVENT_PORT,1U,0U,0U,0U,0U);
    CHECK(nas_xhci_dual_hid_poll_keyboard(&f.s,k)==-ENODEV);
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_halted_gate_for_both_dma_leases(void) {
    struct fixture f;
    CHECK(init_active(&f)==0);
    f.m.fail_stop=true;
    CHECK(nas_xhci_dual_hid_close(&f.s)==-ETIMEDOUT);
    CHECK(f.m.freed==0U);
    CHECK(f.s.may_dma);
    CHECK(f.s.lease.may_dma && f.s.dual_extra_lease.may_dma);
    f.m.fail_stop=false;
    CHECK(cleanup(&f)==0);
    return 0;
}
static int test_slots_and_ports_must_qualify(void) {
    struct fixture f;
    init_io(&f);
    f.m.reg[0x00U/4U]=0x20U;
    f.m.reg[0x04U/4U]=(2U<<24)|1U; /* two ports, only one slot */
    f.m.reg[0x10U/4U]=1U;
    f.m.reg[0x14U/4U]=0x1000U;
    f.m.reg[0x18U/4U]=0x2000U;
    CHECK(nas_xhci_dual_hid_open(&f.io,&f.dma,&f.s)==-ENODEV);
    CHECK(f.s.dual_missing_protocol==1U);
    CHECK(f.m.allocated==NAS_XHCI_RO_DMA_PAGES+
                          NAS_XHCI_DUAL_EXTRA_PAGES);
    CHECK(f.m.freed==0U);
    CHECK(cleanup(&f)==0);

    init_io(&f);
    f.m.reg[0x00U/4U]=0x20U;
    f.m.reg[0x04U/4U]=(1U<<24)|2U; /* two slots, only one port */
    f.m.reg[0x10U/4U]=1U;
    f.m.reg[0x14U/4U]=0x1000U;
    f.m.reg[0x18U/4U]=0x2000U;
    CHECK(nas_xhci_dual_hid_open(&f.io,&f.dma,&f.s)==-ENODEV);
    CHECK(cleanup(&f)==0);
    return 0;
}
int main(void) {
    CHECK(test_interleaved_mouse_before_keyboard()==0);
    CHECK(test_interleaved_keyboard_before_mouse()==0);
    CHECK(test_unrelated_only_remains_cached()==0);
    CHECK(test_short_foreign_and_disconnect()==0);
    CHECK(test_halted_gate_for_both_dma_leases()==0);
    CHECK(test_slots_and_ports_must_qualify()==0);
    puts("[H14 DUAL XHCI HOST PASS] one controller/two slots, reversed event interleavings, queued off-target report without premature rearm, stale/short/disconnect fail-closed, both H6 leases HCHalted-gated; REAL QEMU required");
    return 0;
}
