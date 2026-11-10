/* Small executable xHCI register and DMA lifetime adversary. It models
 * QEMU's fixed xHC capability footprint and intentionally withholds the
 * Enable Slot command completion. No fake event can qualify real USB I/O.
 */
#include "../kernel/drivers/usb/xhci.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"[H11 XHCI HOST FAIL] line %d: %s\n",__LINE__,#x); \
    return 1; \
} } while(0)
#define MMIO_BYTES 0x4000U
#define OP_BASE 0x20U
#define REG_USBCMD (OP_BASE+0x00U)
#define REG_USBSTS (OP_BASE+0x04U)
#define REG_PAGESIZE (OP_BASE+0x08U)
#define REG_PORTSC (OP_BASE+0x400U)
#define STS_HCH (1U<<0)
#define PORT_CCS (1U<<0)
#define PORT_PED (1U<<1)
#define PORT_PR (1U<<4)
#define PORT_PP (1U<<9)

static void *frames[32];
static unsigned allocated,freed;
static bool active_dma,illegal_free;
uint64_t nas_frame_alloc(void) {
    if(allocated>=32U)return 0;
    void *p=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    if(!p)return 0;
    memset(p,0,NAS_DMA_PAGE_BYTES);
    frames[allocated++]=p;
    return (uint64_t)(uintptr_t)p;
}
uint64_t nas_hhdm_offset(void) {return 0;}
bool nas_frame_put(uint64_t pa) {
    if(active_dma){illegal_free=true;return false;}
    for(unsigned i=0;i<allocated;i++) {
        if(frames[i]==(void *)(uintptr_t)pa) {
            free(frames[i]);frames[i]=NULL;freed++;return true;
        }
    }
    return false;
}
struct fake_xhc {
    uint32_t reg[MMIO_BYTES/4U];
    unsigned writes,cmd_doorbells,invalid_mmio;
    bool fail_stop,device_missing,bad_hcc;
};
static int read32(void *ctx,uint32_t offset,uint32_t *value) {
    struct fake_xhc *f=ctx;
    if(!f || !value || (offset&3U) || offset>=MMIO_BYTES) {
        if(f)f->invalid_mmio++;
        return -EINVAL;
    }
    if(offset==0x10U && f->bad_hcc) {
        *value=0;return 0;
    }
    if(offset==REG_PORTSC && f->device_missing){
        *value=f->reg[offset/4U]&~PORT_CCS;return 0;
    }
    *value=f->reg[offset/4U];
    return 0;
}
static int write32(void *ctx,uint32_t offset,uint32_t value) {
    struct fake_xhc *f=ctx;
    if(!f || (offset&3U) || offset>=MMIO_BYTES) {
        if(f)f->invalid_mmio++;
        return -EINVAL;
    }
    f->writes++;
    if(offset==REG_USBCMD) {
        if(value&(1U<<1)) { /* Host controller reset */
            f->reg[offset/4U]=0U;
            f->reg[REG_USBSTS/4U]=STS_HCH;
            active_dma=false;
        } else if(value&1U) {
            f->reg[offset/4U]=value;
            f->reg[REG_USBSTS/4U]&=~STS_HCH;
            active_dma=true;
        } else {
            f->reg[offset/4U]=value;
            if(!f->fail_stop) {
                f->reg[REG_USBSTS/4U]|=STS_HCH;
                active_dma=false;
            }
        }
    } else if(offset==REG_PORTSC && (value&PORT_PR)) {
        f->reg[offset/4U]=PORT_CCS|PORT_PED|PORT_PP|(3U<<10);
    } else if(offset==0x1000U) {
        f->cmd_doorbells++; /* Deliberately do not complete command. */
        f->reg[offset/4U]=value;
    } else f->reg[offset/4U]=value;
    return 0;
}
static void reset(struct fake_xhc *f) {
    for(unsigned i=0;i<allocated;i++) {
        if(frames[i])free(frames[i]);
        frames[i]=NULL;
    }
    allocated=freed=0;
    active_dma=illegal_free=false;
    memset(f,0,sizeof(*f));
    f->reg[0x00U/4U]=0x20U; /* CAPLENGTH */
    f->reg[0x04U/4U]=8U | (8U<<24); /* slots8 ports8 */
    f->reg[0x08U/4U]=0U; /* no scratchpad pages */
    f->reg[0x10U/4U]=0x00080001U; /* QEMU xHCI AC64, 32B contexts */
    f->reg[0x14U/4U]=0x1000U; /* doorbell */
    f->reg[0x18U/4U]=0x2000U; /* runtime */
    f->reg[REG_USBSTS/4U]=STS_HCH;
    f->reg[REG_PAGESIZE/4U]=1U;
    f->reg[REG_PORTSC/4U]=PORT_CCS|PORT_PP|(3U<<10);
}
static void prepare_io(struct fake_xhc *f,struct nas_xhci_io *io) {
    *io=(struct nas_xhci_io){
        .context=f,.mmio_bytes=MMIO_BYTES,
        .read32=read32,.write32=write32
    };
}
static int check_timeout_cleanup(void) {
    struct fake_xhc f;
    struct nas_xhci_io io;
    struct nas_xhci_ro_session session={0};
    reset(&f);prepare_io(&f,&io);
    /* Doorbell is real; event completion deliberately never arrives. */
    int rc=nas_xhci_ro_open(&io,&nas_dma_qemu_coherent,&session);
    CHECK(rc==-ENODEV); /* failed Enable Slot completion is fail-closed */
    CHECK(f.cmd_doorbells>0U);
    CHECK(session.may_dma && session.lease.held==NAS_XHCI_RO_DMA_PAGES);
    CHECK(allocated==NAS_XHCI_RO_DMA_PAGES && freed==0U);
    CHECK(active_dma);
    CHECK(nas_xhci_ro_close(&session)==0);
    CHECK(allocated==NAS_XHCI_RO_DMA_PAGES && freed==NAS_XHCI_RO_DMA_PAGES);
    CHECK(!active_dma && !illegal_free && !session.io && !session.lease.held);
    CHECK(f.invalid_mmio==0U);
    return 0;
}
static int check_unproven_stop_pins(void) {
    struct fake_xhc f;
    struct nas_xhci_io io;
    struct nas_xhci_ro_session session={0};
    reset(&f);prepare_io(&f,&io);
    CHECK(nas_xhci_ro_open(&io,&nas_dma_qemu_coherent,&session)==-ENODEV);
    f.fail_stop=true;
    CHECK(nas_xhci_ro_close(&session)==-ETIMEDOUT);
    CHECK(allocated==NAS_XHCI_RO_DMA_PAGES && freed==0U);
    CHECK(session.may_dma && session.lease.may_dma);
    CHECK(active_dma && !illegal_free);
    f.fail_stop=false;
    CHECK(nas_xhci_ro_close(&session)==0);
    CHECK(freed==NAS_XHCI_RO_DMA_PAGES && !active_dma);
    CHECK(!illegal_free);
    return 0;
}
static int check_no_media_and_bad_caps(void) {
    struct fake_xhc f;
    struct nas_xhci_io io;
    struct nas_xhci_ro_session session={0};
    reset(&f);prepare_io(&f,&io);
    f.device_missing=true;
    CHECK(nas_xhci_ro_open(&io,&nas_dma_qemu_coherent,&session)==-ENODEV);
    CHECK(session.may_dma && allocated==NAS_XHCI_RO_DMA_PAGES);
    CHECK(nas_xhci_ro_close(&session)==0);
    CHECK(freed==NAS_XHCI_RO_DMA_PAGES && !illegal_free);
    reset(&f);prepare_io(&f,&io);
    f.bad_hcc=true;
    CHECK(nas_xhci_ro_open(&io,&nas_dma_qemu_coherent,&session)==-ENODEV);
    CHECK(!session.may_dma && allocated==NAS_XHCI_RO_DMA_PAGES);
    CHECK(nas_xhci_ro_close(&session)==0);
    CHECK(freed==NAS_XHCI_RO_DMA_PAGES && !illegal_free);
    return 0;
}
int main(void) {
    CHECK(check_timeout_cleanup()==0);
    CHECK(check_unproven_stop_pins()==0);
    CHECK(check_no_media_and_bad_caps()==0);
    puts("[H11 XHCI HOST PASS] real MMIO/DMA lease path, uncompleted command timeout, controller-halt fail pins all DMA, clean retry frees 7 pages, no-media, bad HCC; real QEMU still required");
    return 0;
}
