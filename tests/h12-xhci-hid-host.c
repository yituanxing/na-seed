/* H12 xHCI HID poll/interrupt-TRB adversary using actual xhci.c and H6
 * DMA lease. Synthetic host completion only: never claims real USB success. */
#include "../kernel/drivers/usb/xhci.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(p) do { if (!(p)) { \
    fprintf(stderr,"[H12 XHCI HOST FAIL] line %d: %s\n",__LINE__,#p); \
    return 1; \
} } while (0)
#define PAGE 4096U
#define MMIO_BYTES 0x4000U
#define USBCMD 0x20U
#define USBSTS 0x24U
#define PORTSC 0x420U
#define EVENT_TYPE 32U
#define PORT_EVENT_TYPE 34U

struct model {
    uint32_t regs[MMIO_BYTES / 4U];
    bool fail_stop;
    unsigned writes, doorbells, invalid_access, allocated, freed;
};

/* The QEMU coherent provider is linked into the actual dma.c, but this test
 * intentionally uses a distinct host-owned provider and never dereferences
 * real xHCI physical addresses. */
uint64_t nas_frame_alloc(void) { return 0; }
uint64_t nas_hhdm_offset(void) { return 0; }
bool nas_frame_put(uint64_t frame) { (void)frame; return false; }

static int alloc_page(void *context, struct nas_dma_page *page) {
    struct model *m = context;
    void *ptr = aligned_alloc(PAGE, PAGE);
    if (!ptr) return -ENOMEM;
    memset(ptr, 0, PAGE);
    page->cpu = ptr;
    page->bytes = PAGE;
    page->frame_pa = (uint64_t)(uintptr_t)ptr;
    page->bus_address = (uint64_t)(uintptr_t)ptr;
    ++m->allocated;
    return 0;
}
static int free_page(void *context, struct nas_dma_page *page) {
    struct model *m = context;
    free(page->cpu);
    ++m->freed;
    return 0;
}
static int read32(void *context, uint32_t off, uint32_t *out) {
    struct model *m = context;
    if (!out || (off & 3U) || off >= MMIO_BYTES) {
        ++m->invalid_access;
        return -EINVAL;
    }
    *out = m->regs[off / 4U];
    return 0;
}
static int write32(void *context, uint32_t off, uint32_t value) {
    struct model *m = context;
    if ((off & 3U) || off >= MMIO_BYTES) {
        ++m->invalid_access;
        return -EINVAL;
    }
    ++m->writes;
    if (off == USBCMD && !(value & 1U) && !m->fail_stop)
        m->regs[USBSTS / 4U] |= 1U; /* acknowledged HCHalted */
    if (off == 0x1004U) {
        CHECK(value == 3U); /* slot1 interrupt-IN DCI3, not bulk/EP0 */
        ++m->doorbells;
    }
    m->regs[off / 4U] = value;
    return 0;
}

struct fixture {
    struct model model;
    struct nas_xhci_io io;
    struct nas_dma_provider dma;
    struct nas_xhci_ro_session session;
};
static int prepare(struct fixture *f) {
    memset(f, 0, sizeof(*f));
    f->io = (struct nas_xhci_io){
        .context = &f->model, .mmio_bytes = MMIO_BYTES,
        .read32 = read32, .write32 = write32
    };
    f->dma = (struct nas_dma_provider){
        .context = &f->model, .coherent = true,
        .alloc_page = alloc_page, .release_page = free_page
    };
    struct nas_xhci_ro_session *s = &f->session;
    s->io = &f->io;
    s->dma = &f->dma;
    CHECK(nas_dma_lease_begin(&s->lease, &f->dma) == 0);
    for (unsigned i = 0; i < NAS_XHCI_RO_DMA_PAGES; ++i)
        CHECK(nas_dma_lease_alloc_page(&s->lease, &s->pages[i]) == 0);
    CHECK(nas_dma_lease_arm(&s->lease) == 0);
    s->may_dma = true;
    s->hid_mode = true;
    s->ready = true;
    s->hid_pending = true;
    s->slot_id = 1;
    s->hid_in_dci = 3; /* EP address 0x81 */
    s->ep_cycle[1] = 1;
    s->event_cycle = 1;
    s->op_base = 0x20U;
    s->rt_base = 0x2000U;
    s->db_base = 0x1000U;
    s->portsc_offset = PORTSC;
    s->hid_trb_bus = s->pages[5].bus_address + 1024U;
    f->model.regs[USBCMD / 4U] = 1U;
    f->model.regs[PORTSC / 4U] = 3U; /* CCS and PED */
    return 0;
}

static void publish_event(struct fixture *f, uint32_t event_type,
                          uint32_t completion, uint32_t residual,
                          uint64_t trb_pointer, uint8_t endpoint) {
    struct nas_xhci_ro_session *s = &f->session;
    uint8_t *ring = s->pages[2].cpu;
    unsigned offset = (unsigned)s->event_pos * 16U;
    uint32_t status = (completion << 24) | residual;
    uint32_t control = (event_type << 10) |
                        ((uint32_t)endpoint << 16) |
                        ((uint32_t)s->slot_id << 24) | s->event_cycle;
    memcpy(ring + offset, &trb_pointer, 8);
    memcpy(ring + offset + 8, &status, 4);
    memcpy(ring + offset + 12, &control, 4);
}
static int cleanup(struct fixture *f) {
    struct nas_xhci_ro_session *s = &f->session;
    CHECK(nas_xhci_hid_close(s) == 0);
    CHECK(f->model.allocated == NAS_XHCI_RO_DMA_PAGES);
    CHECK(f->model.freed == NAS_XHCI_RO_DMA_PAGES);
    CHECK(!s->io && !s->lease.held && !s->may_dma);
    CHECK(!f->model.invalid_access);
    return 0;
}

static int test_nonblocking_completion_rearm(void) {
    struct fixture f;
    CHECK(prepare(&f) == 0);
    uint8_t report[8];
    memset(report, 0xa5, sizeof(report));
    CHECK(nas_xhci_hid_poll(&f.session, report) == -EAGAIN);
    CHECK(f.model.doorbells == 0);
    CHECK(f.session.ep_pos[1] == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(report[i] == 0xa5);

    const uint8_t a_press[8] = {0,0,4,0,0,0,0,0};
    memcpy(f.session.pages[6].cpu, a_press, sizeof(a_press));
    publish_event(&f, EVENT_TYPE, 1, 0, f.session.hid_trb_bus, 3);
    CHECK(nas_xhci_hid_poll(&f.session, report) == 0);
    CHECK(memcmp(report, a_press, 8) == 0);
    CHECK(f.model.doorbells == 1);
    CHECK(f.session.hid_pending);
    CHECK(f.session.ep_pos[1] == 1);
    CHECK(f.session.hid_trb_bus == f.session.pages[5].bus_address + 1024U);
    CHECK(nas_xhci_hid_poll(&f.session, report) == -EAGAIN);
    CHECK(f.model.doorbells == 1);
    CHECK(cleanup(&f) == 0);
    return 0;
}

static int test_fatal_report_errors(void) {
    struct fixture f;
    uint8_t report[8];
    /* A short 7-byte interrupt packet must never be exposed as a report. */
    CHECK(prepare(&f) == 0);
    memset(report, 0x5a, 8);
    publish_event(&f, EVENT_TYPE, 1, 1, f.session.hid_trb_bus, 3);
    CHECK(nas_xhci_hid_poll(&f.session, report) == -EMSGSIZE);
    CHECK(!f.session.hid_pending && f.model.doorbells == 0);
    for (unsigned i = 0; i < 8; ++i) CHECK(report[i] == 0x5a);
    CHECK(cleanup(&f) == 0);

    CHECK(prepare(&f) == 0);
    publish_event(&f, EVENT_TYPE, 13, 0, f.session.hid_trb_bus, 3);
    CHECK(nas_xhci_hid_poll(&f.session, report) == -EMSGSIZE);
    CHECK(cleanup(&f) == 0);

    CHECK(prepare(&f) == 0);
    publish_event(&f, EVENT_TYPE, 1, 0, f.session.hid_trb_bus + 16U, 3);
    CHECK(nas_xhci_hid_poll(&f.session, report) == -EIO);
    CHECK(f.model.doorbells == 0);
    CHECK(cleanup(&f) == 0);

    CHECK(prepare(&f) == 0);
    publish_event(&f, EVENT_TYPE, 1, 0, f.session.hid_trb_bus, 2);
    CHECK(nas_xhci_hid_poll(&f.session, report) == -EIO);
    CHECK(cleanup(&f) == 0);
    return 0;
}

static int test_disconnect_and_dma_lifetime(void) {
    struct fixture f;
    uint8_t report[8] = {0};
    CHECK(prepare(&f) == 0);
    f.model.regs[PORTSC / 4U] = 0; /* disconnected during poll */
    publish_event(&f, PORT_EVENT_TYPE, 1, 0, 0, 0);
    CHECK(nas_xhci_hid_poll(&f.session, report) == -ENODEV);
    CHECK(f.model.doorbells == 0);
    f.model.fail_stop = true;
    CHECK(nas_xhci_hid_close(&f.session) == -ETIMEDOUT);
    CHECK(f.session.lease.may_dma && f.session.may_dma);
    CHECK(f.model.freed == 0 && f.model.allocated == NAS_XHCI_RO_DMA_PAGES);
    f.model.fail_stop = false;
    CHECK(cleanup(&f) == 0);
    return 0;
}

int main(void) {
    CHECK(test_nonblocking_completion_rearm() == 0);
    CHECK(test_fatal_report_errors() == 0);
    CHECK(test_disconnect_and_dma_lifetime() == 0);
    puts("[H12 XHCI HOST PASS] actual HID poll nonblocking, exact8B, rearm, short/residual rejected, disconnect and unhalted DMA pinned; real QEMU still required");
    return 0;
}
