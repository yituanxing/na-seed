#include "xhci.h"
#include "../../errno.h"
#include <stddef.h>

/*
 * QEMU-only xHCI transport, with separate H11 MSC, H12 keyboard and H13
 * mouse single-session profiles and H14 two-HID-root-port ownership.
 * The Intel xHCI 1.2 specification defines MMIO and TRB structures; QEMU
 * hw/usb/hcd-xhci.c supplies the test fixture. No hub/IRQ/hotplug or
 * physical-host claim. Every live DMA page belongs to a bounded H6 lease.
 */
#define XHCI_CAP_CAPLENGTH 0x00U
#define XHCI_CAP_HCSPARAMS1 0x04U
#define XHCI_CAP_HCSPARAMS2 0x08U
#define XHCI_CAP_HCCPARAMS1 0x10U
#define XHCI_CAP_DBOFF 0x14U
#define XHCI_CAP_RTSOFF 0x18U
#define XHCI_OP_USBCMD 0x00U
#define XHCI_OP_USBSTS 0x04U
#define XHCI_OP_PAGESIZE 0x08U
#define XHCI_OP_CRCR 0x18U
#define XHCI_OP_DCBAAP 0x30U
#define XHCI_OP_CONFIG 0x38U
#define XHCI_OP_PORTSC 0x400U
#define XHCI_PORT_STRIDE 0x10U
#define XHCI_RT_IR0 0x20U
#define XHCI_RT_IMAN 0x00U
#define XHCI_RT_ERSTSZ 0x08U
#define XHCI_RT_ERSTBA 0x10U
#define XHCI_RT_ERDP 0x18U
#define XHCI_CMD_RUN (1U << 0)
#define XHCI_CMD_HCRST (1U << 1)
#define XHCI_STS_HCH (1U << 0)
#define XHCI_STS_CNR (1U << 11)
#define XHCI_PORT_CCS (1U << 0)
#define XHCI_PORT_PED (1U << 1)
#define XHCI_PORT_PR (1U << 4)
#define XHCI_PORT_PP (1U << 9)
#define XHCI_PORT_SPEED_SHIFT 10U
#define XHCI_PORT_SPEED_MASK (15U << XHCI_PORT_SPEED_SHIFT)
#define XHCI_PORT_RW1C ((1U << 17) | (1U << 18) | (1U << 19) | \
                         (1U << 20) | (1U << 21) | (1U << 22) | \
                         (1U << 23))
#define XHCI_TRB_C 1U
#define XHCI_TRB_TC (1U << 1)
#define XHCI_TRB_CHAIN (1U << 4)
#define XHCI_TRB_IOC (1U << 5)
#define XHCI_TRB_IDT (1U << 6)
#define XHCI_TRB_DIR (1U << 16)
#define XHCI_TRB_TYPE(x) ((x) << 10)
#define XHCI_TRB_NORMAL 1U
#define XHCI_TRB_SETUP 2U
#define XHCI_TRB_DATA 3U
#define XHCI_TRB_STATUS 4U
#define XHCI_TRB_LINK 6U
#define XHCI_CMD_ENABLE_SLOT 9U
#define XHCI_CMD_ADDRESS_DEVICE 11U
#define XHCI_CMD_CONFIGURE_EP 12U
#define XHCI_EVENT_TRANSFER 32U
#define XHCI_EVENT_COMMAND 33U
#define XHCI_EVENT_PORT_STATUS 34U
#define XHCI_CC_SUCCESS 1U
#define XHCI_CC_SHORT_PACKET 13U
#define XHCI_POLL_LIMIT 1000000U
#define XHCI_RING_TRBS 64U
#define XHCI_CMD_TRBS 256U
#define XHCI_EVENT_TRBS 256U
#define XHCI_DCBAA_ERST_OFF 2048U
#define XHCI_PAYLOAD_MAX 4096U
#define XHCI_FTDI_PACKET_BYTES 64U
#define XHCI_FTDI_HEADER_BYTES 2U
#define XHCI_FTDI_IN_OFFSET 128U

enum xhci_page_index {
    XHCI_P_DCBAA,
    XHCI_P_COMMAND,
    XHCI_P_EVENT,
    XHCI_P_INPUT,
    XHCI_P_OUTPUT,
    XHCI_P_ENDPOINTS,
    XHCI_P_PAYLOAD
};
struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed,aligned(16)));

static void zero_bytes(void *dst, uint32_t count) {
    uint8_t *p = (uint8_t *)dst;
    for (uint32_t i = 0; i < count; ++i) p[i] = 0;
}
static void copy_bytes(void *dst, const void *src, uint32_t count) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < count; ++i) d[i] = s[i];
}
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void put32(void *dst, uint32_t v) {
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static int io_read(struct nas_xhci_ro_session *s, uint32_t off,
                   uint32_t *value) {
    if (!s || !s->io || !s->io->read32 || !value || (off & 3U) ||
        off > s->io->mmio_bytes || s->io->mmio_bytes - off < 4U)
        return -EINVAL;
    return s->io->read32(s->io->context, off, value);
}
static int io_write(struct nas_xhci_ro_session *s, uint32_t off,
                    uint32_t value) {
    if (!s || !s->io || !s->io->write32 || (off & 3U) ||
        off > s->io->mmio_bytes || s->io->mmio_bytes - off < 4U)
        return -EINVAL;
    return s->io->write32(s->io->context, off, value);
}
static int io_write64(struct nas_xhci_ro_session *s, uint32_t off,
                      uint64_t value) {
    int rc = io_write(s, off, (uint32_t)value);
    if (!rc) rc = io_write(s, off + 4U, (uint32_t)(value >> 32));
    return rc;
}
static int op_read(struct nas_xhci_ro_session *s, uint32_t off,
                   uint32_t *value) {
    return io_read(s, s->op_base + off, value);
}
static int op_write(struct nas_xhci_ro_session *s, uint32_t off,
                    uint32_t value) {
    return io_write(s, s->op_base + off, value);
}
static int op_write64(struct nas_xhci_ro_session *s, uint32_t off,
                      uint64_t value) {
    return io_write64(s, s->op_base + off, value);
}
/* Dual mode uses exactly three supplementary leased pages, with no alias
 * between the two independent output contexts/endpoint rings/HID payloads.
 * The input context is reused only while both slots are being enumerated.
 */
static unsigned physical_page(struct nas_xhci_ro_session *s, unsigned logical) {
    if (s->dual_mode && s->dual_current == 1U &&
        logical >= XHCI_P_OUTPUT && logical <= XHCI_P_PAYLOAD)
        return logical + (NAS_XHCI_RO_DMA_PAGES - XHCI_P_OUTPUT);
    return logical;
}
static struct nas_dma_page *leased_page(struct nas_xhci_ro_session *s,
                                        unsigned index) {
    if (index < NAS_XHCI_RO_DMA_PAGES) return &s->pages[index];
    index -= NAS_XHCI_RO_DMA_PAGES;
    if (index >= NAS_XHCI_DUAL_EXTRA_PAGES || !s->dual_mode)
        return NULL;
    return &s->dual_extra_pages[index];
}
static int dma_sync(struct nas_xhci_ro_session *s, unsigned page,
                    uint32_t off, uint32_t length,
                    enum nas_dma_direction direction) {
    unsigned index = physical_page(s,page);
    struct nas_dma_page *p = leased_page(s,index);
    if (!p) return -EINVAL;
    struct nas_dma_lease *l = index < NAS_XHCI_RO_DMA_PAGES ?
                             &s->lease : &s->dual_extra_lease;
    return nas_dma_lease_sync(l,p,off,length,direction);
}
static uint8_t *page_cpu(struct nas_xhci_ro_session *s, unsigned page) {
    struct nas_dma_page *p = leased_page(s,physical_page(s,page));
    return p ? (uint8_t *)p->cpu : NULL;
}
static uint64_t page_bus(struct nas_xhci_ro_session *s, unsigned page) {
    struct nas_dma_page *p = leased_page(s,physical_page(s,page));
    return p ? p->bus_address : 0;
}
static int poll_bits(struct nas_xhci_ro_session *s, uint32_t off,
                     uint32_t mask, uint32_t wanted) {
    for (uint32_t i = 0; i < XHCI_POLL_LIMIT; ++i) {
        uint32_t v;
        if (io_read(s, off, &v)) return -EIO;
        if ((v & mask) == wanted) return 0;
        __asm__ volatile("" ::: "memory");
    }
    return -ETIMEDOUT;
}
static int quiesce(struct nas_xhci_ro_session *s) {
    uint32_t command;
    if (op_read(s, XHCI_OP_USBCMD, &command)) return -EIO;
    if (op_write(s, XHCI_OP_USBCMD, command & ~XHCI_CMD_RUN))
        return -EIO;
    return poll_bits(s, s->op_base + XHCI_OP_USBSTS,
                     XHCI_STS_HCH, XHCI_STS_HCH);
}
static struct xhci_trb *ring_base(struct nas_xhci_ro_session *s,
                                  unsigned index) {
    return (struct xhci_trb *)(void *)(page_cpu(s, XHCI_P_ENDPOINTS) +
                                        (index * 1024U));
}
static uint64_t ring_bus(struct nas_xhci_ro_session *s, unsigned index) {
    return page_bus(s, XHCI_P_ENDPOINTS) + index * 1024U;
}
static int ring_enqueue(struct nas_xhci_ro_session *s, unsigned which,
                        const struct xhci_trb *trb, uint64_t *trb_bus) {
    struct xhci_trb *base;
    uint16_t *position;
    uint8_t *cycle;
    uint16_t limit;
    uint64_t bus;
    unsigned page;
    uint32_t byte_base;
    if (which > 3U || !trb || !trb_bus) return -EINVAL;
    if (!which) {
        base = (struct xhci_trb *)(void *)page_cpu(s, XHCI_P_COMMAND);
        bus = page_bus(s, XHCI_P_COMMAND);
        position = &s->cmd_pos;
        cycle = &s->cmd_cycle;
        limit = XHCI_CMD_TRBS;
        page = XHCI_P_COMMAND;
        byte_base = 0;
    } else {
        unsigned i = which - 1U;
        base = ring_base(s, i);
        bus = ring_bus(s, i);
        position = &s->ep_pos[i];
        cycle = &s->ep_cycle[i];
        limit = XHCI_RING_TRBS;
        page = XHCI_P_ENDPOINTS;
        byte_base = i * 1024U;
    }
    /* Slot limit-1 is a Cycle Toggle link back to the same DMA segment.
     * As all transfers are synchronous, no producer overwrites a live TRB.
     */
    if (*position >= limit - 1U) {
        struct xhci_trb *link = &base[limit - 1U];
        zero_bytes(link, sizeof(*link));
        link->parameter = bus;
        link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) |
                        XHCI_TRB_TC | (*cycle & 1U);
        if (dma_sync(s, page, byte_base +
                     (limit - 1U) * sizeof(*link), sizeof(*link),
                     NAS_DMA_TO_DEVICE)) return -EIO;
        *position = 0;
        *cycle ^= 1U;
    }
    uint16_t pos = *position;
    struct xhci_trb next = *trb;
    next.control = (next.control & ~XHCI_TRB_C) | (*cycle & 1U);
    base[pos] = next;
    if (dma_sync(s, page, byte_base + pos * sizeof(next),
                 sizeof(next), NAS_DMA_TO_DEVICE)) return -EIO;
    *trb_bus = bus + pos * sizeof(next);
    ++*position;
    return 0;
}
static int ring_doorbell(struct nas_xhci_ro_session *s, uint8_t slot,
                         uint32_t target) {
    return io_write(s, s->db_base + (uint32_t)slot * 4U, target);
}
static int event_next(struct nas_xhci_ro_session *s,
                      struct xhci_trb *event) {
    struct xhci_trb *ring =
        (struct xhci_trb *)(void *)page_cpu(s, XHCI_P_EVENT);
    if (dma_sync(s, XHCI_P_EVENT,
                 s->event_pos * sizeof(struct xhci_trb),
                 sizeof(struct xhci_trb), NAS_DMA_FROM_DEVICE))
        return -EIO;
    struct xhci_trb found = ring[s->event_pos];
    if ((found.control & XHCI_TRB_C) != s->event_cycle) return -EAGAIN;
    *event = found;
    ++s->event_pos;
    if (s->event_pos == XHCI_EVENT_TRBS) {
        s->event_pos = 0;
        s->event_cycle ^= 1U;
    }
    /* ERDP.EHB=1 acknowledges observed event-busy, while masked interrupts
     * keep the entire H11 profile polling-only.
     */
    if (io_write64(s, s->rt_base + XHCI_RT_IR0 + XHCI_RT_ERDP,
        page_bus(s, XHCI_P_EVENT) +
        s->event_pos * sizeof(struct xhci_trb) + (1ULL << 3)))
        return -EIO;
    return 0;
}
static int await_event(struct nas_xhci_ro_session *s, uint32_t kind,
                       uint64_t trb_ptr, uint8_t endpoint,
                       uint8_t *slot_out, uint32_t *residual_out) {
    for (uint32_t tick = 0; tick < XHCI_POLL_LIMIT; ++tick) {
        struct xhci_trb event;
        int rc = event_next(s, &event);
        if (rc == -EAGAIN) continue;
        if (rc) return rc;
        uint32_t type = (event.control >> 10) & 63U;
        if (type == XHCI_EVENT_PORT_STATUS) continue;
        if (type != kind || (event.parameter & ~0xFULL) != trb_ptr)
            return -EIO;
        uint32_t code = (event.status >> 24) & 255U;
        s->debug_completion_code = code;
        s->debug_event_type = type;
        if (kind == XHCI_EVENT_TRANSFER) {
            if (((event.control >> 16) & 31U) != endpoint ||
                ((event.control >> 24) & 255U) != s->slot_id)
                return -EIO;
        }
        if (slot_out) *slot_out = (uint8_t)(event.control >> 24);
        if (residual_out) *residual_out = event.status & 0xffffffU;
        if (code == XHCI_CC_SUCCESS) return 0;
        if (code == XHCI_CC_SHORT_PACKET) return -EMSGSIZE;
        return -EIO;
    }
    return -ETIMEDOUT;
}
static int command(struct nas_xhci_ro_session *s, uint64_t parameter,
                   uint32_t type, uint8_t slot,
                   uint8_t *slot_out) {
    struct xhci_trb trb;
    uint64_t trb_bus;
    zero_bytes(&trb, sizeof(trb));
    trb.parameter = parameter;
    trb.control = XHCI_TRB_TYPE(type) | ((uint32_t)slot << 24);
    int rc = ring_enqueue(s, 0, &trb, &trb_bus);
    if (!rc) rc = ring_doorbell(s, 0, 0);
    uint8_t completed_slot=0;
    if (!rc) rc = await_event(s, XHCI_EVENT_COMMAND,
                              trb_bus, 0, &completed_slot, NULL);
    if (rc) return rc;
    if (type != XHCI_CMD_ENABLE_SLOT && completed_slot != slot)
        return -EIO; /* command target must match the returned xHC slot */
    if (slot_out) *slot_out=completed_slot;
    return 0;
}
static void context_dword(struct nas_xhci_ro_session *s,
                          uint32_t context_index, uint32_t dword,
                          uint32_t value) {
    put32(page_cpu(s, XHCI_P_INPUT) +
          context_index * s->context_bytes + dword * 4U, value);
}
static int sync_input(struct nas_xhci_ro_session *s) {
    return dma_sync(s, XHCI_P_INPUT, 0, NAS_DMA_PAGE_BYTES,
                    NAS_DMA_TO_DEVICE);
}
static int enable_next_port(struct nas_xhci_ro_session *s, uint32_t max_ports,
                            uint8_t excluded_port) {
    s->debug_stage = NAS_XHCI_STAGE_PORT;
    if (!max_ports || max_ports > 15U) return -ENODEV;
    for (uint32_t p = 1; p <= max_ports; ++p) {
        if (p == excluded_port) continue;
        uint32_t off = s->op_base + XHCI_OP_PORTSC +
                       (p - 1U) * XHCI_PORT_STRIDE;
        uint32_t v;
        if (io_read(s, off, &v)) return -EIO;
        s->debug_portsc = v;
        if (!(v & XHCI_PORT_CCS)) continue;
        /* USB2 High Speed begins Connected but *not* Enabled in QEMU.
         * Reset port before handing its speed and root-port ID to slot ctx.
         * Do not echo PORTSC write-one-to-clear change flags.
         */
        if (!(v & XHCI_PORT_PED)) {
            uint32_t writable = v & ~(XHCI_PORT_RW1C |
                                      XHCI_PORT_PED |
                                      (15U << 5) |
                                      (1U << 16));
            if (io_write(s, off, writable | XHCI_PORT_PR |
                          XHCI_PORT_PP)) return -EIO;
            if (poll_bits(s, off, XHCI_PORT_PR, 0)) return -ETIMEDOUT;
        }
        if (io_read(s, off, &v)) return -EIO;
        s->debug_portsc = v;
        uint8_t speed = (uint8_t)((v & XHCI_PORT_SPEED_MASK) >>
                                    XHCI_PORT_SPEED_SHIFT);
        if (!(v & XHCI_PORT_PED) || !(v & XHCI_PORT_CCS) ||
            speed < 1U || speed > 4U) return -ENODEV;
        s->port = (uint8_t)p;
        s->portsc_offset = off;
        s->port_speed = speed;
        return 0;
    }
    return -ENODEV;
}
static int enable_port(struct nas_xhci_ro_session *s, uint32_t max_ports) {
    return enable_next_port(s,max_ports,0);
}
/* Snapshot only the per-slot state. Command/event producer/consumer positions
 * remain controller-global and cannot be rewound when switching devices.
 */
static void dual_capture(struct nas_xhci_ro_session *s) {
    struct nas_xhci_dual_hid_channel *c =
        &s->dual_channels[s->dual_current];
    c->portsc_offset=s->portsc_offset;
    c->max_packet0=s->max_packet0;
    c->hid_packet=s->hid_packet;
    for (unsigned i=0;i<3U;++i) {
        c->ep_pos[i]=s->ep_pos[i];
        c->ep_cycle[i]=s->ep_cycle[i];
    }
    c->slot_id=s->slot_id;
    c->port=s->port;
    c->port_speed=s->port_speed;
    c->interface_number=s->interface_number;
    c->hid_in_dci=s->hid_in_dci;
    c->hid_interval=s->hid_interval;
    c->hid_protocol=s->hid_protocol;
    c->hid_report_bytes=s->hid_report_bytes;
    c->hid_trb_bus=s->hid_trb_bus;
    c->hid_pending=s->hid_pending;
}
static void dual_select(struct nas_xhci_ro_session *s,unsigned which) {
    if (s->dual_current==which) return;
    dual_capture(s);
    s->dual_current=(uint8_t)which;
    const struct nas_xhci_dual_hid_channel *c=&s->dual_channels[which];
    s->portsc_offset=c->portsc_offset;
    s->max_packet0=c->max_packet0;
    s->hid_packet=c->hid_packet;
    for (unsigned i=0;i<3U;++i) {
        s->ep_pos[i]=c->ep_pos[i];
        s->ep_cycle[i]=c->ep_cycle[i];
    }
    s->slot_id=c->slot_id;
    s->port=c->port;
    s->port_speed=c->port_speed;
    s->interface_number=c->interface_number;
    s->hid_in_dci=c->hid_in_dci;
    s->hid_interval=c->hid_interval;
    s->hid_protocol=c->hid_protocol;
    s->hid_report_bytes=c->hid_report_bytes;
    s->hid_trb_bus=c->hid_trb_bus;
    s->hid_pending=c->hid_pending;
}
static void dual_clear_active(struct nas_xhci_ro_session *s,
                              uint8_t protocol,uint8_t bytes) {
    s->port=0; s->port_speed=0; s->portsc_offset=0;
    s->slot_id=0; s->max_packet0=0;
    s->interface_number=0; s->hid_in_dci=0;
    s->hid_packet=0; s->hid_interval=0; s->hid_trb_bus=0;
    s->hid_pending=false; s->hid_protocol=protocol;
    s->hid_report_bytes=bytes;
    for (unsigned i=0;i<3U;++i) {
        s->ep_pos[i]=0; s->ep_cycle[i]=1U;
    }
}
static int setup_context(struct nas_xhci_ro_session *s) {
    s->debug_stage = NAS_XHCI_STAGE_ENABLE_SLOT;
    uint8_t slot;
    int rc = command(s, 0, XHCI_CMD_ENABLE_SLOT, 0, &slot);
    if (rc || !slot || slot > 8U) return -ENODEV;
    s->slot_id = slot;
    /* DCBAA entry indexed by xHC slot ID. */
    ((uint64_t *)(void *)page_cpu(s, XHCI_P_DCBAA))[slot] =
        page_bus(s, XHCI_P_OUTPUT);
    if (dma_sync(s, XHCI_P_DCBAA, slot * 8U, 8,
                 NAS_DMA_TO_DEVICE)) return -EIO;
    zero_bytes(page_cpu(s, XHCI_P_INPUT), NAS_DMA_PAGE_BYTES);
    /* Input Control Context add: Slot Context bit0 + EP0 Context bit1. */
    put32(page_cpu(s, XHCI_P_INPUT) + 4U, 3U);
    /* Slot Context index=1, EP0 index=2 for Input Context. */
    context_dword(s, 1, 0, ((uint32_t)s->port_speed << 20) | (1U << 27));
    context_dword(s, 1, 1, (uint32_t)s->port << 16);
    /* EP0 ring is at region zero in the endpoint DMA ring page. */
    uint16_t mps = s->port_speed == 4U ? 512U :
                   s->port_speed == 3U ? 64U : 8U;
    s->max_packet0 = mps;
    context_dword(s, 2, 1, ((uint32_t)mps << 16) |
                              (4U << 3) | (3U << 1));
    uint64_t ring = ring_bus(s, 0) | 1ULL;
    context_dword(s, 2, 2, (uint32_t)ring);
    context_dword(s, 2, 3, (uint32_t)(ring >> 32));
    context_dword(s, 2, 4, 8U);
    if (sync_input(s)) return -EIO;
    s->debug_stage = NAS_XHCI_STAGE_ADDRESS_DEVICE;
    return command(s, page_bus(s, XHCI_P_INPUT),
                   XHCI_CMD_ADDRESS_DEVICE, slot, NULL);
}
static int control_request(struct nas_xhci_ro_session *s,
                           uint8_t bmrequest, uint8_t request,
                           uint16_t value, uint16_t index, uint16_t bytes,
                           void *destination) {
    if (!s || bytes > 512U || (bytes && !destination)) return -EINVAL;
    uint8_t setup[8] = {bmrequest, request, (uint8_t)value,
                        (uint8_t)(value >> 8), (uint8_t)index,
                        (uint8_t)(index >> 8), (uint8_t)bytes,
                        (uint8_t)(bytes >> 8)};
    uint64_t immediate = 0;
    for (unsigned i = 0; i < 8; ++i)
        immediate |= (uint64_t)setup[i] << (i * 8U);
    bool in = (bmrequest & 0x80U) != 0U;
    if (bytes && !in) return -EINVAL; /* H11 never sends USB payload writes. */
    struct xhci_trb trb;
    uint64_t event_ptr = 0;
    zero_bytes(&trb, sizeof(trb));
    trb.parameter = immediate;
    trb.status = 8;
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_SETUP) |
                  XHCI_TRB_IDT | XHCI_TRB_CHAIN |
                  (bytes ? 3U << 16 : 0U);
    int rc = ring_enqueue(s, 1, &trb, &event_ptr);
    if (rc) return rc;
    if (bytes) {
        zero_bytes(page_cpu(s, XHCI_P_PAYLOAD), bytes);
        if (dma_sync(s, XHCI_P_PAYLOAD, 0, bytes,
                     NAS_DMA_FROM_DEVICE)) return -EIO;
        zero_bytes(&trb, sizeof(trb));
        trb.parameter = page_bus(s, XHCI_P_PAYLOAD);
        trb.status = bytes;
        trb.control = XHCI_TRB_TYPE(XHCI_TRB_DATA) |
                      XHCI_TRB_CHAIN | XHCI_TRB_DIR;
        if (ring_enqueue(s, 1, &trb, &event_ptr)) return -EIO;
    }
    zero_bytes(&trb, sizeof(trb));
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC |
                  (in ? 0U : XHCI_TRB_DIR);
    if (ring_enqueue(s, 1, &trb, &event_ptr)) return -EIO;
    if (ring_doorbell(s, s->slot_id, 1)) return -EIO;
    rc = await_event(s, XHCI_EVENT_TRANSFER, event_ptr, 1, NULL, NULL);
    if (rc) return rc;
    if (bytes) {
        if (dma_sync(s, XHCI_P_PAYLOAD, 0, bytes,
                     NAS_DMA_FROM_DEVICE)) return -EIO;
        copy_bytes(destination, page_cpu(s, XHCI_P_PAYLOAD), bytes);
    }
    return 0;
}
static int read_usb_descriptors(struct nas_xhci_ro_session *s) {
    s->debug_stage = NAS_XHCI_STAGE_GET_DEVICE_DESC;
    uint8_t device[18];
    int rc = control_request(s, 0x80, 6, 0x100, 0, sizeof(device), device);
    if (rc || device[0] < 18U || device[1] != 1U) return -ENODEV;
    uint8_t first_config[9];
    s->debug_stage = NAS_XHCI_STAGE_GET_CONFIG_HEADER;
    rc = control_request(s, 0x80, 6, 0x200, 0,
                         sizeof(first_config), first_config);
    if (rc || first_config[0] < 9U || first_config[1] != 2U)
        return -ENODEV;
    uint16_t total = le16(&first_config[2]);
    if (total < 18U || total > 512U) return -E2BIG;
    uint8_t config[512];
    s->debug_stage = NAS_XHCI_STAGE_GET_CONFIG_FULL;
    zero_bytes(config, sizeof(config));
    rc = control_request(s, 0x80, 6, 0x200, 0, total, config);
    if (rc) return rc;
    uint8_t configured = 0;
    uint8_t out_ep = 0, in_ep = 0;
    uint16_t out_packet = 0, in_packet = 0;
    uint8_t iface = 0;
    bool in_bot_iface = false, found_bot = false;
    for (uint32_t off = 0; off + 2U <= total;) {
        uint8_t len = config[off], type = config[off + 1U];
        if (len < 2U || off + len > total) return -ENODEV;
        if (type == 2U && len >= 9U)
            configured = config[off + 5U];
        else if (type == 4U && len >= 9U) {
            in_bot_iface = config[off + 5U] == 8U &&
                           config[off + 6U] == 6U &&
                           config[off + 7U] == 0x50U &&
                           config[off + 3U] == 0U;
            if (in_bot_iface && !found_bot) {
                iface = config[off + 2U];
                found_bot = true;
            }
        } else if (type == 5U && len >= 7U && in_bot_iface) {
            uint8_t ep = config[off + 2U];
            uint16_t mps = le16(&config[off + 4U]) & 0x7ffU;
            if ((config[off + 3U] & 3U) == 2U &&
                (ep & 15U) != 0U && (ep & 15U) <= 15U &&
                mps && mps <= 1024U) {
                if ((ep & 0x80U) && !in_ep) {
                    in_ep = ep;
                    in_packet = mps;
                } else if (!(ep & 0x80U) && !out_ep) {
                    out_ep = ep;
                    out_packet = mps;
                }
            }
        }
        off += len;
    }
    if (!configured || !found_bot || !in_ep || !out_ep ||
        (in_ep & 15U) > 7U || (out_ep & 15U) > 7U)
        return -ENODEV;
    s->interface_number = iface;
    s->bulk_out_dci = (uint8_t)((out_ep & 15U) * 2U);
    s->bulk_in_dci = (uint8_t)((in_ep & 15U) * 2U + 1U);
    s->max_packet_out = out_packet;
    s->max_packet_in = in_packet;
    /* Standard USB SET_CONFIGURATION uses a zero data stage. */
    s->debug_stage = NAS_XHCI_STAGE_SET_CONFIG;
    rc = control_request(s, 0, 9, configured, 0, 0, NULL);
    if (rc) return rc;
    return 0;
}
static void make_bulk_ctx(struct nas_xhci_ro_session *s, uint8_t dci,
                          uint16_t packet, uint8_t ring_index,
                          uint8_t usb_endpoint_type) {
    uint32_t ctx = (uint32_t)dci + 1U;
    uint64_t r = ring_bus(s, ring_index) | 1ULL;
    context_dword(s, ctx, 0, 0U);
    context_dword(s, ctx, 1, ((uint32_t)packet << 16) |
                  ((uint32_t)usb_endpoint_type << 3) | (3U << 1));
    context_dword(s, ctx, 2, (uint32_t)r);
    context_dword(s, ctx, 3, (uint32_t)(r >> 32));
    context_dword(s, ctx, 4, 1024U);
}
static int configure_bulk_endpoints(struct nas_xhci_ro_session *s) {
    s->debug_stage = NAS_XHCI_STAGE_CONFIGURE_EP;
    if (s->bulk_out_dci == s->bulk_in_dci ||
        s->bulk_out_dci >= 31U || s->bulk_in_dci >= 31U)
        return -EINVAL;
    zero_bytes(page_cpu(s, XHCI_P_INPUT), NAS_DMA_PAGE_BYTES);
    uint32_t mask = 1U | (1U << s->bulk_out_dci) |
                    (1U << s->bulk_in_dci);
    /* QEMU expects add bits: Slot Context plus new endpoints, but not EP0. */
    put32(page_cpu(s, XHCI_P_INPUT) + 4U, mask);
    uint8_t entries = s->bulk_in_dci > s->bulk_out_dci ?
                      s->bulk_in_dci : s->bulk_out_dci;
    context_dword(s, 1, 0, ((uint32_t)s->port_speed << 20) |
                               ((uint32_t)entries << 27));
    context_dword(s, 1, 1, (uint32_t)s->port << 16);
    make_bulk_ctx(s, s->bulk_out_dci, s->max_packet_out, 1, 2);
    make_bulk_ctx(s, s->bulk_in_dci, s->max_packet_in, 2, 6);
    if (sync_input(s)) return -EIO;
    return command(s, page_bus(s, XHCI_P_INPUT),
                   XHCI_CMD_CONFIGURE_EP, s->slot_id, NULL);
}

/* Boot HID devices are exclusively enumerated in their own H12/H13
 * controller session: do not turn H11's BOT-only lifetime into a shared
 * multi-device stack. The same control ring, xHC commands and leased DMA
 * buffers are reused, with one Interrupt-IN endpoint instead of BOT bulk.
 */
static int read_hid_boot_descriptors(struct nas_xhci_ro_session *s) {
    uint8_t device[18];
    s->debug_stage = NAS_XHCI_STAGE_HID_GET_DEVICE_DESC;
    int rc = control_request(s, 0x80U, 6U, 0x100U, 0,
                             sizeof(device), device);
    if (rc || device[0] < 18U || device[1] != 1U) return -ENODEV;
    uint8_t config_header[9];
    s->debug_stage = NAS_XHCI_STAGE_HID_GET_CONFIG_HEADER;
    rc = control_request(s, 0x80U, 6U, 0x200U, 0,
                         sizeof(config_header), config_header);
    if (rc || config_header[0] < 9U || config_header[1] != 2U)
        return -ENODEV;
    uint16_t total = le16(config_header + 2U);
    if (total < 18U || total > 512U) return -E2BIG;
    uint8_t config[512];
    zero_bytes(config, sizeof(config));
    s->debug_stage = NAS_XHCI_STAGE_HID_GET_CONFIG_FULL;
    rc = control_request(s, 0x80U, 6U, 0x200U, 0, total, config);
    if (rc) return rc;
    uint8_t config_value = 0, endpoint = 0, interval = 0;
    uint16_t packet = 0;
    bool found_hid = false, selected_interface = false;
    for (uint32_t off = 0; off + 2U <= total;) {
        uint8_t length = config[off];
        uint8_t type = config[off + 1U];
        if (length < 2U || off + length > total) return -ENODEV;
        if (type == 2U && length >= 9U) {
            config_value = config[off + 5U];
        } else if (type == 4U && length >= 9U) {
            bool boot_hid = config[off + 3U] == 0U &&
                                 config[off + 4U] >= 1U &&
                                 config[off + 5U] == 3U &&
                                 config[off + 6U] == 1U &&
                                 config[off + 7U] == s->hid_protocol;
            selected_interface = boot_hid && !found_hid;
            if (selected_interface) {
                found_hid = true;
                s->interface_number = config[off + 2U];
            }
        } else if (type == 5U && length >= 7U &&
                   selected_interface && !endpoint) {
            uint8_t address = config[off + 2U];
            uint16_t max_packet = le16(config + off + 4U) & 0x7ffU;
            uint8_t binterval = config[off + 6U];
            if ((address & 0x80U) && (address & 15U) >= 1U &&
                (address & 15U) <= 7U &&
                (config[off + 3U] & 3U) == 3U &&
                max_packet >= s->hid_report_bytes && max_packet <= 1024U &&
                binterval >= 1U) {
                endpoint = address;
                packet = max_packet;
                interval = binterval;
            }
        }
        off += length;
    }
    if (!config_value || !found_hid || !endpoint || !interval)
        return -ENODEV;
    s->hid_in_dci = (uint8_t)((endpoint & 15U) * 2U + 1U);
    s->hid_packet = packet;
    /* USB 2.0 FS/LS interrupt bInterval is in milliseconds; xHC interval
     * units are power-of-two 125us microframes. floor(log2(ms)) + 3.
     * HS/SS interrupt bInterval is itself log2(microframes)+1.
     */
    if (s->port_speed <= 2U) {
        uint8_t exp = 0;
        while (exp < 7U && (1U << (exp + 1U)) <= interval) ++exp;
        s->hid_interval = (uint8_t)(exp + 3U);
    } else {
        if (interval > 16U) return -ENODEV;
        s->hid_interval = (uint8_t)(interval - 1U);
    }
    s->debug_stage = NAS_XHCI_STAGE_HID_SET_CONFIG;
    rc = control_request(s, 0U, 9U, config_value, 0, 0, NULL);
    if (rc) return rc;
    /* HID class SET_PROTOCOL(0) selects Boot protocol. QEMU's keyboard
     * uses an exact 8-byte report; QEMU's Boot Mouse uses 4 bytes.
     * Never interpret an arbitrary report descriptor as a Boot device.
     */
    s->debug_stage = NAS_XHCI_STAGE_HID_SET_PROTOCOL;
    return control_request(s, 0x21U, 0x0bU, 0,
                           s->interface_number, 0, NULL);
}

static int configure_hid_interrupt_endpoint(struct nas_xhci_ro_session *s) {
    s->debug_stage = NAS_XHCI_STAGE_HID_CONFIGURE_EP;
    if (!s->hid_in_dci || s->hid_in_dci >= 31U ||
        !s->hid_packet || s->hid_interval > 15U) return -EINVAL;
    zero_bytes(page_cpu(s, XHCI_P_INPUT), NAS_DMA_PAGE_BYTES);
    /* Slot Context bit0 plus exactly the one new interrupt IN DCI. EP0
     * remains in the existing output context after Address Device.
     */
    put32(page_cpu(s, XHCI_P_INPUT) + 4U,
          1U | (1U << s->hid_in_dci));
    context_dword(s, 1, 0, ((uint32_t)s->port_speed << 20) |
                           ((uint32_t)s->hid_in_dci << 27));
    context_dword(s, 1, 1, (uint32_t)s->port << 16);
    uint32_t ctx = (uint32_t)s->hid_in_dci + 1U;
    uint64_t dequeue = ring_bus(s, 1U) | 1ULL;
    context_dword(s, ctx, 0, (uint32_t)s->hid_interval << 16);
    context_dword(s, ctx, 1, ((uint32_t)s->hid_packet << 16) |
                              (7U << 3) | (3U << 1));
    context_dword(s, ctx, 2, (uint32_t)dequeue);
    context_dword(s, ctx, 3, (uint32_t)(dequeue >> 32));
    context_dword(s, ctx, 4, 8U | (8U << 16));
    if (sync_input(s)) return -EIO;
    return command(s, page_bus(s, XHCI_P_INPUT),
                   XHCI_CMD_CONFIGURE_EP, s->slot_id, NULL);
}

static int hid_arm_interrupt_in(struct nas_xhci_ro_session *s) {
    if (s->hid_pending || !s->hid_in_dci) return -EINVAL;
    const uint8_t bytes = s->hid_report_bytes ? s->hid_report_bytes : 8U;
    if (bytes != 4U && bytes != 8U) return -EINVAL;
    s->debug_stage = NAS_XHCI_STAGE_HID_ARM;
    zero_bytes(page_cpu(s, XHCI_P_PAYLOAD), bytes);
    if (dma_sync(s, XHCI_P_PAYLOAD, 0, bytes, NAS_DMA_FROM_DEVICE))
        return -EIO;
    struct xhci_trb transfer;
    zero_bytes(&transfer, sizeof(transfer));
    transfer.parameter = page_bus(s, XHCI_P_PAYLOAD);
    transfer.status = bytes;
    transfer.control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;
    uint64_t trb_bus;
    int rc = ring_enqueue(s, 2U, &transfer, &trb_bus);
    if (rc) return rc;
    rc = ring_doorbell(s, s->slot_id, s->hid_in_dci);
    if (rc) return rc;
    s->hid_trb_bus = trb_bus;
    s->hid_pending = true;
    s->debug_stage = NAS_XHCI_STAGE_HID_READY;
    return 0;
}
static int run_controller(struct nas_xhci_ro_session *s) {
    s->debug_stage = NAS_XHCI_STAGE_CAPS;
    uint32_t cap0, slots, hcs2, hcc, dboff, rtsoff, pagesize;
    if (io_read(s, XHCI_CAP_CAPLENGTH, &cap0) ||
        io_read(s, XHCI_CAP_HCSPARAMS1, &slots) ||
        io_read(s, XHCI_CAP_HCSPARAMS2, &hcs2) ||
        io_read(s, XHCI_CAP_HCCPARAMS1, &hcc) ||
        io_read(s, XHCI_CAP_DBOFF, &dboff) ||
        io_read(s, XHCI_CAP_RTSOFF, &rtsoff))
        return -EIO;
    s->op_base = cap0 & 255U;
    s->rt_base = rtsoff & ~31U;
    s->db_base = dboff & ~3U;
    s->context_bytes = (hcc & (1U << 2)) ? 64U : 32U;
    uint32_t max_ports = (slots >> 24) & 255U;
    uint32_t max_slots = slots & 255U;
    uint32_t scratchpad = ((hcs2 >> 27) & 31U) |
                          (((hcs2 >> 21) & 31U) << 5);
    if (s->op_base < 0x20U || s->op_base > 0x1000U ||
        s->rt_base < s->op_base || s->db_base < s->op_base ||
        s->rt_base >= s->io->mmio_bytes ||
        s->db_base >= s->io->mmio_bytes ||
        max_slots < (s->dual_mode ? 2U : 1U) ||
        max_ports < (s->dual_mode ? 2U : 1U) || max_ports > 15U ||
        scratchpad != 0U || !(hcc & 1U))
        return -ENODEV;
    s->dual_max_ports=(uint8_t)max_ports;
    s->debug_stage = NAS_XHCI_STAGE_HALT;
    if (quiesce(s)) return -EIO;
    s->debug_stage = NAS_XHCI_STAGE_RESET;
    if (op_write(s, XHCI_OP_USBCMD, XHCI_CMD_HCRST)) return -EIO;
    if (poll_bits(s, s->op_base + XHCI_OP_USBCMD,
                  XHCI_CMD_HCRST, 0) ||
        poll_bits(s, s->op_base + XHCI_OP_USBSTS,
                  XHCI_STS_CNR, 0)) return -ETIMEDOUT;
    if (op_read(s, XHCI_OP_PAGESIZE, &pagesize) ||
        !(pagesize & 1U)) return -ENODEV;
    s->debug_stage = NAS_XHCI_STAGE_RING_CONFIG;
    /* Initialize command and event rings before exposing DMA to xHC. */
    zero_bytes(page_cpu(s, XHCI_P_DCBAA), NAS_DMA_PAGE_BYTES);
    zero_bytes(page_cpu(s, XHCI_P_COMMAND), NAS_DMA_PAGE_BYTES);
    zero_bytes(page_cpu(s, XHCI_P_EVENT), NAS_DMA_PAGE_BYTES);
    zero_bytes(page_cpu(s, XHCI_P_INPUT), NAS_DMA_PAGE_BYTES);
    zero_bytes(page_cpu(s, XHCI_P_OUTPUT), NAS_DMA_PAGE_BYTES);
    zero_bytes(page_cpu(s, XHCI_P_ENDPOINTS), NAS_DMA_PAGE_BYTES);
    zero_bytes(page_cpu(s, XHCI_P_PAYLOAD), NAS_DMA_PAGE_BYTES);
    s->cmd_cycle = s->event_cycle = 1U;
    for (unsigned i = 0; i < 3; ++i) s->ep_cycle[i] = 1U;
    uint8_t *erst = page_cpu(s, XHCI_P_DCBAA) + XHCI_DCBAA_ERST_OFF;
    uint64_t event_bus = page_bus(s, XHCI_P_EVENT);
    put32(erst, (uint32_t)event_bus);
    put32(erst + 4, (uint32_t)(event_bus >> 32));
    put32(erst + 8, XHCI_EVENT_TRBS);
    for (unsigned i = 0; i < NAS_XHCI_RO_DMA_PAGES; ++i) {
        if (dma_sync(s, i, 0, NAS_DMA_PAGE_BYTES,
                     NAS_DMA_TO_DEVICE)) return -EIO;
    }
    if (s->dual_mode) {
        for (unsigned i = 0; i < NAS_XHCI_DUAL_EXTRA_PAGES; ++i) {
            struct nas_dma_page *p = &s->dual_extra_pages[i];
            zero_bytes(p->cpu,NAS_DMA_PAGE_BYTES);
            if (nas_dma_lease_sync(&s->dual_extra_lease,p,0,
                NAS_DMA_PAGE_BYTES,NAS_DMA_TO_DEVICE)) return -EIO;
        }
    }
    /* Runtime interrupter 0: masked, event ring segment count exactly 1. */
    if (io_write(s, s->rt_base + XHCI_RT_IR0 + XHCI_RT_IMAN, 0) ||
        io_write(s, s->rt_base + XHCI_RT_IR0 + XHCI_RT_ERSTSZ, 1) ||
        io_write64(s, s->rt_base + XHCI_RT_IR0 + XHCI_RT_ERSTBA,
                  page_bus(s, XHCI_P_DCBAA) + XHCI_DCBAA_ERST_OFF) ||
        io_write64(s, s->rt_base + XHCI_RT_IR0 + XHCI_RT_ERDP,
                  event_bus) ||
        op_write64(s, XHCI_OP_DCBAAP, page_bus(s, XHCI_P_DCBAA)) ||
        op_write64(s, XHCI_OP_CRCR,
                   page_bus(s, XHCI_P_COMMAND) | 1ULL) ||
        op_write(s, XHCI_OP_CONFIG, s->dual_mode ? 2U : 1U))
        return -EIO;
    /* Both independent page leases must be armed BEFORE RUN can DMA. If
     * either fails, dual_close() quiesces any previously armed lease.
     */
    if (s->dual_mode &&
        nas_dma_lease_arm(&s->dual_extra_lease)) return -EIO;
    if (nas_dma_lease_arm(&s->lease)) return -EIO;
    s->may_dma = true;
    s->debug_stage = NAS_XHCI_STAGE_RUN;
    if (op_write(s, XHCI_OP_USBCMD, XHCI_CMD_RUN)) return -EIO;
    if (poll_bits(s, s->op_base + XHCI_OP_USBSTS, XHCI_STS_HCH, 0))
        return -ETIMEDOUT;
    return enable_port(s, max_ports);
}
int nas_xhci_ro_open(const struct nas_xhci_io *io,
                     const struct nas_dma_provider *dma,
                     struct nas_xhci_ro_session *s) {
    if (!io || !dma || !s || !io->read32 || !io->write32 ||
        io->mmio_bytes < 0x1000U || s->io || s->lease.held)
        return -EINVAL;
    s->io = io;
    s->dma = dma;
    s->debug_stage = NAS_XHCI_STAGE_DMA;
    int rc = nas_dma_lease_begin(&s->lease, dma);
    if (rc) return rc;
    for (unsigned i = 0; i < NAS_XHCI_RO_DMA_PAGES; ++i) {
        rc = nas_dma_lease_alloc_page(&s->lease, &s->pages[i]);
        if (rc) return rc;
    }
    rc = run_controller(s);
    if (rc) return rc;
    rc = setup_context(s);
    if (rc) return rc;
    rc = read_usb_descriptors(s);
    if (rc) return rc;
    rc = configure_bulk_endpoints(s);
    if (rc) return rc;
    s->debug_stage = NAS_XHCI_STAGE_USB_MSC_READY;
    s->ready = true;
    return 0;
}

static int hid_open_mode(const struct nas_xhci_io *io,
                         const struct nas_dma_provider *dma,
                         struct nas_xhci_ro_session *s,
                         uint8_t protocol, uint8_t report_bytes) {
    if (!io || !dma || !s || !io->read32 || !io->write32 ||
        io->mmio_bytes < 0x1000U || s->io || s->lease.held ||
        (protocol != 1U && protocol != 2U) ||
        (report_bytes != 8U && report_bytes != 4U))
        return -EINVAL;
    s->io = io;
    s->dma = dma;
    s->hid_mode = true;
    s->hid_protocol = protocol;
    s->hid_report_bytes = report_bytes;
    s->debug_stage = NAS_XHCI_STAGE_DMA;
    int rc = nas_dma_lease_begin(&s->lease, dma);
    if (rc) return rc;
    for (unsigned i = 0; i < NAS_XHCI_RO_DMA_PAGES; ++i) {
        rc = nas_dma_lease_alloc_page(&s->lease, &s->pages[i]);
        if (rc) return rc;
    }
    rc = run_controller(s);
    if (rc) return rc;
    rc = setup_context(s);
    if (rc) return rc;
    rc = read_hid_boot_descriptors(s);
    if (rc) return rc;
    rc = configure_hid_interrupt_endpoint(s);
    if (rc) return rc;
    rc = hid_arm_interrupt_in(s);
    if (rc) return rc;
    s->ready = true;
    return 0;
}

int nas_xhci_hid_open(const struct nas_xhci_io *io,
                      const struct nas_dma_provider *dma,
                      struct nas_xhci_ro_session *s) {
    return hid_open_mode(io,dma,s,1U,8U);
}
int nas_xhci_hid_mouse_open(const struct nas_xhci_io *io,
                            const struct nas_dma_provider *dma,
                            struct nas_xhci_ro_session *s) {
    return hid_open_mode(io,dma,s,2U,4U);
}

static int hid_poll_mode(struct nas_xhci_ro_session *s, uint8_t *report,
                         uint8_t protocol, uint8_t bytes) {
    if (!s || !report || !s->ready || !s->hid_mode ||
        s->dual_mode || !s->hid_pending || s->io_active) return -EINVAL;
    /* The zero defaults preserve the isolated H12 host fixture's legacy
     * directly constructed session; real open() always sets both fields. */
    const uint8_t actual_protocol = s->hid_protocol ? s->hid_protocol : 1U;
    const uint8_t actual_bytes = s->hid_report_bytes ? s->hid_report_bytes : 8U;
    if (actual_protocol != protocol || actual_bytes != bytes)
        return -EINVAL;
    s->debug_stage = NAS_XHCI_STAGE_HID_POLL;
    /* A single controller-event dequeue is nonblocking. Port change
     * events are bounded and never masquerade as a HID report.
     */
    for (unsigned skipped = 0; skipped < 4U; ++skipped) {
        struct xhci_trb event;
        int rc = event_next(s, &event);
        if (rc) return rc;
        uint32_t kind = (event.control >> 10) & 63U;
        if (kind == XHCI_EVENT_PORT_STATUS) {
            uint32_t portsc;
            if (io_read(s, s->portsc_offset, &portsc)) return -EIO;
            s->debug_portsc = portsc;
            if (!(portsc & XHCI_PORT_CCS) || !(portsc & XHCI_PORT_PED))
                return -ENODEV;
            continue;
        }
        if (kind != XHCI_EVENT_TRANSFER ||
            (event.parameter & ~0xFULL) != s->hid_trb_bus ||
            ((event.control >> 16) & 31U) != s->hid_in_dci ||
            ((event.control >> 24) & 255U) != s->slot_id)
            return -EIO;
        uint32_t completion = (event.status >> 24) & 255U;
        uint32_t residual = event.status & 0xffffffU;
        s->debug_completion_code = completion;
        s->debug_event_type = kind;
        s->hid_pending = false;
        if (completion == XHCI_CC_SHORT_PACKET || residual != 0U)
            return -EMSGSIZE;
        if (completion != XHCI_CC_SUCCESS) return -EIO;
        rc = dma_sync(s, XHCI_P_PAYLOAD, 0, bytes, NAS_DMA_FROM_DEVICE);
        if (rc) return -EIO;
        uint8_t received[8];
        copy_bytes(received, page_cpu(s, XHCI_P_PAYLOAD), bytes);
        rc = hid_arm_interrupt_in(s);
        if (rc) return rc;
        copy_bytes(report, received, bytes);
        return 0;
    }
    return -EAGAIN;
}
int nas_xhci_hid_poll(struct nas_xhci_ro_session *s, uint8_t report[8]) {
    return hid_poll_mode(s,report,1U,8U);
}
int nas_xhci_hid_mouse_poll(struct nas_xhci_ro_session *s, uint8_t report[4]) {
    return hid_poll_mode(s,report,2U,4U);
}
/* H14 enumerates two directly connected USB2 HID devices on one physical
 * QEMU xHC and only resets/starts the controller ONCE. Port order is a strict
 * fixture contract (Boot Keyboard then Boot Mouse), never a fallback.
 * All three extra DMA pages belong to a separate lease armed before RUN.
 */
int nas_xhci_dual_hid_open(const struct nas_xhci_io *io,
                            const struct nas_dma_provider *dma,
                            struct nas_xhci_ro_session *s) {
    if (!io || !dma || !s || !io->read32 || !io->write32 ||
        io->mmio_bytes < 0x1000U || s->io || s->lease.held ||
        s->dual_extra_lease.held || s->dual_mode)
        return -EINVAL;
    s->io=io;
    s->dma=dma;
    s->hid_mode=true;
    s->dual_mode=true;
    s->dual_missing_protocol=1U;
    dual_clear_active(s,1U,8U);
    s->debug_stage=NAS_XHCI_STAGE_DMA;
    int rc=nas_dma_lease_begin(&s->lease,dma);
    if (rc) return rc;
    for (unsigned i=0;i<NAS_XHCI_RO_DMA_PAGES;++i) {
        rc=nas_dma_lease_alloc_page(&s->lease,&s->pages[i]);
        if (rc) return rc;
    }
    rc=nas_dma_lease_begin(&s->dual_extra_lease,dma);
    if (rc) return rc;
    for (unsigned i=0;i<NAS_XHCI_DUAL_EXTRA_PAGES;++i) {
        rc=nas_dma_lease_alloc_page(&s->dual_extra_lease,
                                     &s->dual_extra_pages[i]);
        if (rc) return rc;
    }
    rc=run_controller(s);
    if (rc) return rc;
    rc=setup_context(s);
    if (rc) return rc;
    rc=read_hid_boot_descriptors(s);
    if (rc) return rc;
    rc=configure_hid_interrupt_endpoint(s);
    if (rc) return rc;
    const uint8_t keyboard_port=s->port;
    const uint8_t keyboard_slot=s->slot_id;
    dual_capture(s);
    s->dual_current=1U;
    dual_clear_active(s,2U,4U);
    s->dual_missing_protocol=2U;
    rc=enable_next_port(s,s->dual_max_ports,keyboard_port);
    if (rc) return rc;
    rc=setup_context(s);
    if (rc) return rc;
    if (s->port == keyboard_port || s->slot_id == keyboard_slot)
        return -EIO;
    rc=read_hid_boot_descriptors(s);
    if (rc) return rc;
    rc=configure_hid_interrupt_endpoint(s);
    if (rc) return rc;
    dual_capture(s);
    /* Arm asynchronous reports only AFTER both EP0 enumerations finish,
     * since the shared input context/control payload is overwritten there.
     */
    dual_select(s,0U);
    rc=hid_arm_interrupt_in(s);
    if (rc) return rc;
    dual_select(s,1U);
    rc=hid_arm_interrupt_in(s);
    if (rc) return rc;
    dual_select(s,0U);
    s->ready=true;
    s->dual_missing_protocol=0U;
    return 0;
}
static int dual_hid_poll(struct nas_xhci_ro_session *s,unsigned wanted,
                          uint8_t *report,uint8_t bytes) {
    if (!s || !report || !s->ready || !s->dual_mode ||
        !s->hid_mode || s->io_active || wanted >= 2U ||
        bytes != (wanted ? 4U : 8U))
        return -EINVAL;
    struct nas_xhci_dual_hid_channel *channels=s->dual_channels;
    if (channels[wanted].complete_pending) {
        /* An off-target completion stays unarmed until its own consumer
         * actually reads it. This bounds its queue to one report and
         * prevents a valid A,A,B burst from overflowing while B polls.
         */
        dual_select(s,wanted);
        int rc=hid_arm_interrupt_in(s);
        if (rc) return rc;
        dual_capture(s);
        copy_bytes(report,channels[wanted].complete_report,bytes);
        channels[wanted].complete_pending=false;
        return 0;
    }
    s->debug_stage=NAS_XHCI_STAGE_HID_POLL;
    /* Shared event-ring completion may belong to either device: never
     * drop the other slot's report. Each channel has an independent
     * one-report ready queue, and a separate armed TRB at any one time.
     */
    for (unsigned skip=0;skip<8U;++skip) {
        struct xhci_trb event;
        int rc=event_next(s,&event);
        if (rc) return rc;
        const uint32_t kind=(event.control>>10)&63U;
        if (kind == XHCI_EVENT_PORT_STATUS) {
            for (unsigned k=0;k<2U;++k) {
                uint32_t v;
                if (io_read(s,channels[k].portsc_offset,&v)) return -EIO;
                if (!(v & XHCI_PORT_CCS) || !(v & XHCI_PORT_PED))
                    return -ENODEV;
            }
            continue;
        }
        if (kind != XHCI_EVENT_TRANSFER) return -EIO;
        unsigned matched=2U;
        for (unsigned k=0;k<2U;++k) {
            if ((event.parameter & ~0xFULL)==channels[k].hid_trb_bus &&
                ((event.control>>16)&31U)==channels[k].hid_in_dci &&
                ((event.control>>24)&255U)==channels[k].slot_id &&
                channels[k].hid_pending) {
                matched=k;
                break;
            }
        }
        if (matched==2U || channels[matched].complete_pending)
            return -EIO; /* foreign or overflowing transfer: fail closed */
        dual_select(s,matched);
        uint32_t completion=(event.status>>24)&255U;
        uint32_t residual=event.status&0xffffffU;
        s->debug_completion_code=completion;
        s->debug_event_type=kind;
        s->hid_pending=false;
        if (completion==XHCI_CC_SHORT_PACKET || residual)
            return -EMSGSIZE;
        if (completion!=XHCI_CC_SUCCESS) return -EIO;
        uint8_t actual_bytes=s->hid_report_bytes;
        if (actual_bytes != (matched ? 4U : 8U)) return -EIO;
        rc=dma_sync(s,XHCI_P_PAYLOAD,0,actual_bytes,NAS_DMA_FROM_DEVICE);
        if (rc) return -EIO;
        uint8_t received[8];
        copy_bytes(received,page_cpu(s,XHCI_P_PAYLOAD),actual_bytes);
        dual_capture(s);
        channels[matched].complete_pending=true;
        copy_bytes(channels[matched].complete_report,received,actual_bytes);
        if (matched==wanted) {
            /* The requested consumer takes this report immediately;
             * only then may its own next TRB be armed.
             */
            rc=hid_arm_interrupt_in(s);
            if (rc) return rc;
            dual_capture(s);
            copy_bytes(report,received,actual_bytes);
            channels[matched].complete_pending=false;
            return 0;
        }
        /* Foreign slot is buffered but deliberately NOT rearmed until its
         * caller consumes the cached event, avoiding queue overflow. */
    }
    return -EAGAIN;
}
int nas_xhci_dual_hid_poll_keyboard(struct nas_xhci_ro_session *s,
                                    uint8_t report[8]) {
    return dual_hid_poll(s,0U,report,8U);
}
int nas_xhci_dual_hid_poll_mouse(struct nas_xhci_ro_session *s,
                                 uint8_t report[4]) {
    return dual_hid_poll(s,1U,report,4U);
}
int nas_xhci_dual_hid_close(struct nas_xhci_ro_session *s) {
    if (!s || !s->io || !s->dma || !s->dual_mode || s->io_active)
        return -EINVAL;
    /* HCHalted FIRST: on failure not even a supplementary lease is
     * disarmed, freed or made safe for external BAR unmapping.
     */
    if (s->may_dma || s->lease.may_dma ||
        s->dual_extra_lease.may_dma) {
        int rc=quiesce(s);
        if (rc) return rc;
        if (s->lease.may_dma) {
            rc=nas_dma_lease_disarm_after_quiesce(&s->lease);
            if (rc) return rc;
        }
        if (s->dual_extra_lease.may_dma) {
            rc=nas_dma_lease_disarm_after_quiesce(&s->dual_extra_lease);
            if (rc) return rc;
        }
        s->may_dma=false;
    }
    for (unsigned i=0;i<NAS_XHCI_DUAL_EXTRA_PAGES;++i) {
        if (s->dual_extra_pages[i].cpu) {
            int rc=nas_dma_lease_free_page(&s->dual_extra_lease,
                                           &s->dual_extra_pages[i]);
            if (rc) return rc;
        }
    }
    for (unsigned i=0;i<NAS_XHCI_RO_DMA_PAGES;++i) {
        if (s->pages[i].cpu) {
            int rc=nas_dma_lease_free_page(&s->lease,&s->pages[i]);
            if (rc) return rc;
        }
    }
    if (s->dual_extra_lease.provider &&
        nas_dma_lease_end(&s->dual_extra_lease)) return -EIO;
    if (s->lease.provider && nas_dma_lease_end(&s->lease)) return -EIO;
    zero_bytes(s,sizeof(*s));
    return 0;
}

static int bulk_transfer(struct nas_xhci_ro_session *s, bool in,
                         void *buffer, uint32_t bytes) {
    if (!s || !s->ready || s->hid_mode || s->dual_mode ||
        s->ftdi_mode || s->io_active || !buffer ||
        !bytes || bytes > XHCI_PAYLOAD_MAX) return -EINVAL;
    uint8_t dci = in ? s->bulk_in_dci : s->bulk_out_dci;
    unsigned which = in ? 3U : 2U; /* EP ring IDs 2/3 */
    s->debug_stage = NAS_XHCI_STAGE_BULK;
    s->io_active = true;
    int rc = 0;
    if (!in) {
        copy_bytes(page_cpu(s, XHCI_P_PAYLOAD), buffer, bytes);
        rc = dma_sync(s, XHCI_P_PAYLOAD, 0, bytes, NAS_DMA_TO_DEVICE);
    } else {
        zero_bytes(page_cpu(s, XHCI_P_PAYLOAD), bytes);
        rc = dma_sync(s, XHCI_P_PAYLOAD, 0, bytes, NAS_DMA_FROM_DEVICE);
    }
    if (rc) { s->io_active = false; return rc; }
    struct xhci_trb trb;
    uint64_t trb_bus;
    zero_bytes(&trb, sizeof(trb));
    trb.parameter = page_bus(s, XHCI_P_PAYLOAD);
    trb.status = bytes;
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;
    rc = ring_enqueue(s, which, &trb, &trb_bus);
    if (!rc) rc = ring_doorbell(s, s->slot_id, dci);
    uint32_t residual = 0;
    if (!rc) rc = await_event(s, XHCI_EVENT_TRANSFER, trb_bus,
                              dci, NULL, &residual);
    if (!rc && residual != 0U) rc = -EMSGSIZE;
    if (!rc && in) {
        rc = dma_sync(s, XHCI_P_PAYLOAD, 0, bytes, NAS_DMA_FROM_DEVICE);
        if (!rc) copy_bytes(buffer, page_cpu(s, XHCI_P_PAYLOAD), bytes);
    }
    s->io_active = false;
    return rc;
}
int nas_xhci_ro_bulk_out(struct nas_xhci_ro_session *s,
                          const void *src, uint32_t bytes) {
    return bulk_transfer(s, false, (void *)src, bytes);
}
int nas_xhci_ro_bulk_in(struct nas_xhci_ro_session *s,
                         void *dst, uint32_t bytes) {
    return bulk_transfer(s, true, dst, bytes);
}
int nas_xhci_ro_close(struct nas_xhci_ro_session *s) {
    if (!s || !s->io || !s->dma || s->io_active || s->dual_mode)
        return -EINVAL;
    if (s->may_dma) {
        /* Failure must NOT release any DMA or allow BAR unmap. */
        int rc = quiesce(s);
        if (rc) return rc;
        rc = nas_dma_lease_disarm_after_quiesce(&s->lease);
        if (rc) return rc;
        s->may_dma = false;
    }
    for (unsigned i = 0; i < NAS_XHCI_RO_DMA_PAGES; ++i) {
        if (s->pages[i].cpu) {
            int rc = nas_dma_lease_free_page(&s->lease,
                                              &s->pages[i]);
            if (rc) return rc;
        }
    }
    int rc = nas_dma_lease_end(&s->lease);
    if (rc) return rc;
    zero_bytes(s, sizeof(*s));
    return 0;
}

int nas_xhci_hid_close(struct nas_xhci_ro_session *s) {
    if (!s || !s->hid_mode || s->hid_protocol == 2U) return -EINVAL;
    return nas_xhci_ro_close(s);
}
int nas_xhci_hid_mouse_close(struct nas_xhci_ro_session *s) {
    if (!s || !s->hid_mode || s->hid_protocol != 2U) return -EINVAL;
    return nas_xhci_ro_close(s);
}

/*
 * H15 FT232BM: a deliberately narrow, non-BOT QEMU USB serial transport.
 * Vendor/product, interface and BOTH endpoint addresses must match QEMU's
 * hw/usb/dev-serial.c. A BOT MSC or a HID device cannot enter this mode.
 * The serial device owns an independent xHC/controller lifetime, not an
 * endpoint on an H11 or H14 controller already in use.
 */
static int ftdi_descriptors(struct nas_xhci_ro_session *s) {
    uint8_t device[18], header[9], config[512];
    s->debug_stage = NAS_XHCI_STAGE_FTDI_DESCRIPTOR;
    int rc = control_request(s, 0x80U, 6U, 0x0100U, 0U,
                             sizeof(device), device);
    if (rc) return rc;
    if (device[0] != 18U || device[1] != 1U ||
        device[7] != 8U || device[8] != 0x03U ||
        device[9] != 0x04U || device[10] != 0x01U ||
        device[11] != 0x60U || device[17] != 1U)
        return -ENODEV; /* 0403:6001, one configuration, EP0 8B. */
    rc = control_request(s, 0x80U, 6U, 0x0200U, 0U,
                         sizeof(header), header);
    if (rc) return rc;
    if (header[0] != 9U || header[1] != 2U ||
        header[4] != 1U || header[5] != 1U)
        return -ENODEV;
    uint16_t total = le16(header + 2U);
    if (total < 32U || total > sizeof(config)) return -ENODEV;
    zero_bytes(config, sizeof(config));
    rc = control_request(s, 0x80U, 6U, 0x0200U, 0U, total, config);
    if (rc) return rc;
    if (config[0] != 9U || config[1] != 2U ||
        le16(config + 2U) != total || config[4] != 1U ||
        config[5] != 1U) return -ENODEV;
    bool iface_found = false, iface_active = false;
    bool in_found = false, out_found = false;
    unsigned interfaces = 0U, endpoints = 0U;
    for (uint32_t off = 0; off + 2U <= total;) {
        uint8_t length = config[off], type = config[off + 1U];
        if (length < 2U || off + length > total) return -ENODEV;
        if (type == 4U) {
            if (length < 9U) return -ENODEV;
            ++interfaces;
            /* Reject arbitrary vendor interfaces, alternate settings and
             * QEMU usb-braille (whose interface/VID differ from usb-serial).
             */
            iface_active = !iface_found && config[off + 2U] == 0U &&
                           config[off + 3U] == 0U &&
                           config[off + 4U] == 2U &&
                           config[off + 5U] == 0xffU &&
                           config[off + 6U] == 0xffU &&
                           config[off + 7U] == 0xffU;
            if (iface_active) {
                iface_found = true;
                s->interface_number = 0U;
            }
        } else if (type == 5U && iface_active) {
            if (length < 7U) return -ENODEV;
            uint8_t ep = config[off + 2U];
            uint16_t packet = le16(config + off + 4U) & 0x07ffU;
            ++endpoints;
            if ((config[off + 3U] & 3U) != 2U ||
                packet != XHCI_FTDI_PACKET_BYTES) return -ENODEV;
            if (ep == 0x81U && !in_found) {
                in_found = true;
            } else if (ep == 0x02U && !out_found) {
                out_found = true;
            } else {
                return -ENODEV;
            }
        }
        off += length;
    }
    if (!iface_found || interfaces != 1U || endpoints != 2U ||
        !in_found || !out_found || s->port_speed != 1U)
        return -ENODEV; /* Full Speed QEMU FT232BM only. */
    s->bulk_in_dci = 3U;  /* endpoint 1 IN */
    s->bulk_out_dci = 4U; /* endpoint 2 OUT */
    s->max_packet_in = XHCI_FTDI_PACKET_BYTES;
    s->max_packet_out = XHCI_FTDI_PACKET_BYTES;
    return control_request(s, 0U, 9U, 1U, 0U, 0U, NULL);
}

/* QEMU FT232BM vendor OUT: RESET, 115200baud divisor26, 8N1, DTR+RTS.
 * These are requests with zero data stages and EP0 STATUS completion.
 */
static int ftdi_vendor_setup(struct nas_xhci_ro_session *s) {
    s->debug_stage = NAS_XHCI_STAGE_FTDI_VENDOR_INIT;
    int rc = control_request(s, 0x40U, 0U, 0U, 0U, 0U, NULL);
    if (rc) return rc;
    rc = control_request(s, 0x40U, 3U, 0x001aU, 0U, 0U, NULL);
    if (rc) return rc;
    rc = control_request(s, 0x40U, 4U, 0x0008U, 0U, 0U, NULL);
    if (rc) return rc;
    return control_request(s, 0x40U, 1U, 0x0303U, 0U, 0U, NULL);
}

static int ftdi_arm_bulk_in(struct nas_xhci_ro_session *s) {
    if (!s || !s->ftdi_mode || !s->ready ||
        s->ftdi_pending || s->ftdi_rx_len ||
        s->bulk_in_dci != 3U) return -EINVAL;
    s->debug_stage = NAS_XHCI_STAGE_FTDI_ARM_IN;
    zero_bytes(page_cpu(s, XHCI_P_PAYLOAD) + XHCI_FTDI_IN_OFFSET,
               XHCI_FTDI_PACKET_BYTES);
    int rc = dma_sync(s, XHCI_P_PAYLOAD, XHCI_FTDI_IN_OFFSET,
                      XHCI_FTDI_PACKET_BYTES, NAS_DMA_FROM_DEVICE);
    if (rc) return rc;
    struct xhci_trb trb;
    zero_bytes(&trb, sizeof(trb));
    trb.parameter = page_bus(s, XHCI_P_PAYLOAD) + XHCI_FTDI_IN_OFFSET;
    trb.status = XHCI_FTDI_PACKET_BYTES;
    trb.control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;
    uint64_t trb_bus = 0U;
    rc = ring_enqueue(s, 3U, &trb, &trb_bus);
    if (rc) return rc;
    s->ftdi_in_trb_bus = trb_bus;
    s->ftdi_pending = true;
    rc = ring_doorbell(s, s->slot_id, s->bulk_in_dci);
    if (rc) {
        /* The enqueued TRB may already be visible to the controller.
         * Retain its pending owner until explicit quiescence/close. */
        return rc;
    }
    return 0;
}

static int ftdi_event_port(struct nas_xhci_ro_session *s) {
    uint32_t portsc = 0U;
    if (io_read(s, s->portsc_offset, &portsc)) return -EIO;
    s->debug_portsc = portsc;
    return (portsc & (XHCI_PORT_CCS | XHCI_PORT_PED)) ==
           (XHCI_PORT_CCS | XHCI_PORT_PED) ? 0 : -ENODEV;
}

/* Accept exactly the previously queued FTDI IN TRB. The xHCI transfer
 * event residual is relative to its requested 64B, not the user bytes.
 * Both successful and short packet completions are legal for usb-serial.
 * QEMU always puts two status bytes BEFORE up to 62 user bytes.
 */
static int ftdi_receive_event(struct nas_xhci_ro_session *s,
                              const struct xhci_trb *event) {
    if (!s->ftdi_pending || s->ftdi_rx_len ||
        ((event->control >> 10) & 63U) != XHCI_EVENT_TRANSFER ||
        (event->parameter & ~0xFULL) != s->ftdi_in_trb_bus ||
        ((event->control >> 16) & 31U) != s->bulk_in_dci ||
        ((event->control >> 24) & 255U) != s->slot_id)
        return -EIO;
    uint32_t completion = (event->status >> 24) & 255U;
    uint32_t residual = event->status & 0xffffffU;
    s->debug_completion_code = completion;
    s->debug_event_type = XHCI_EVENT_TRANSFER;
    if (completion != XHCI_CC_SHORT_PACKET &&
        completion != XHCI_CC_SUCCESS) return -EIO;
    if (residual > XHCI_FTDI_PACKET_BYTES) return -EMSGSIZE;
    uint32_t actual = XHCI_FTDI_PACKET_BYTES - residual;
    if (actual < XHCI_FTDI_HEADER_BYTES) return -EMSGSIZE;
    if (dma_sync(s, XHCI_P_PAYLOAD, XHCI_FTDI_IN_OFFSET,
                 XHCI_FTDI_PACKET_BYTES, NAS_DMA_FROM_DEVICE))
        return -EIO;
    const uint8_t *packet = page_cpu(s, XHCI_P_PAYLOAD) +
                            XHCI_FTDI_IN_OFFSET;
    if (!(packet[0] & 1U)) return -EIO; /* QEMU modem status sentinel */
    /* THRE/TEMT (0x60) are ordinary FTDI UART status flags; QEMU
     * currently reports 0. Real line errors (OE/PE/FE/BI) are not data.
     */
    if (packet[1] & 0x1eU) return -EIO;
    s->ftdi_pending = false;
    s->ftdi_in_trb_bus = 0U;
    s->ftdi_rx_offset = 0U;
    s->ftdi_rx_len = (uint8_t)(actual - XHCI_FTDI_HEADER_BYTES);
    if (s->ftdi_rx_len)
        copy_bytes(s->ftdi_rx_buffer, packet + XHCI_FTDI_HEADER_BYTES,
                   s->ftdi_rx_len);
    return 0;
}

int nas_xhci_ftdi_open(const struct nas_xhci_io *io,
                       const struct nas_dma_provider *dma,
                       struct nas_xhci_ro_session *s) {
    if (!io || !dma || !s || !io->read32 || !io->write32 ||
        io->mmio_bytes < 0x1000U || s->io || s->lease.held)
        return -EINVAL;
    s->io = io;
    s->dma = dma;
    s->ftdi_mode = true;
    s->debug_stage = NAS_XHCI_STAGE_DMA;
    int rc = nas_dma_lease_begin(&s->lease, dma);
    if (rc) return rc;
    for (unsigned i = 0U; i < NAS_XHCI_RO_DMA_PAGES; ++i) {
        rc = nas_dma_lease_alloc_page(&s->lease, &s->pages[i]);
        if (rc) return rc;
    }
    rc = run_controller(s);
    if (rc) return rc;
    rc = setup_context(s);
    if (rc) return rc;
    rc = ftdi_descriptors(s);
    if (rc) return rc;
    rc = configure_bulk_endpoints(s);
    if (rc) return rc;
    rc = ftdi_vendor_setup(s);
    if (rc) return rc;
    s->debug_stage = NAS_XHCI_STAGE_FTDI_READY;
    s->ready = true;
    return ftdi_arm_bulk_in(s);
}

/* One bounded event dequeue; FTDI NAK is an ordinary no-data state,
 * not a synthetic empty user packet. Neither busy wait nor timeout here.
 */
int nas_xhci_ftdi_poll_in(struct nas_xhci_ro_session *s, uint8_t *dst,
                          uint32_t capacity, uint32_t *received) {
    if (!received) return -EINVAL;
    *received = 0U;
    if (!s || !dst || capacity == 0U || !s->ready ||
        !s->ftdi_mode || s->hid_mode || s->dual_mode ||
        s->io_active) return -EINVAL;
    s->debug_stage = NAS_XHCI_STAGE_FTDI_POLL_IN;
    if (s->ftdi_rx_len) {
        uint32_t count = s->ftdi_rx_len;
        if (count > capacity) count = capacity;
        copy_bytes(dst, s->ftdi_rx_buffer + s->ftdi_rx_offset, count);
        s->ftdi_rx_offset = (uint8_t)(s->ftdi_rx_offset + count);
        s->ftdi_rx_len = (uint8_t)(s->ftdi_rx_len - count);
        if (!s->ftdi_rx_len) s->ftdi_rx_offset = 0U;
        *received = count;
        return 0;
    }
    if (!s->ftdi_pending) {
        int rc = ftdi_arm_bulk_in(s);
        return rc ? rc : -EAGAIN;
    }
    for (unsigned skipped = 0U; skipped < 4U; ++skipped) {
        struct xhci_trb event;
        int rc = event_next(s, &event);
        if (rc) return rc;
        uint32_t kind = (event.control >> 10) & 63U;
        if (kind == XHCI_EVENT_PORT_STATUS) {
            rc = ftdi_event_port(s);
            if (rc) return rc;
            continue;
        }
        rc = ftdi_receive_event(s, &event);
        if (rc) return rc;
        if (!s->ftdi_rx_len) {
            rc = ftdi_arm_bulk_in(s);
            return rc ? rc : -EAGAIN;
        }
        uint32_t count = s->ftdi_rx_len;
        if (count > capacity) count = capacity;
        copy_bytes(dst, s->ftdi_rx_buffer, count);
        s->ftdi_rx_offset = (uint8_t)count;
        s->ftdi_rx_len = (uint8_t)(s->ftdi_rx_len - count);
        if (!s->ftdi_rx_len) s->ftdi_rx_offset = 0U;
        *received = count;
        return 0;
    }
    return -EAGAIN;
}

int nas_xhci_ftdi_write(struct nas_xhci_ro_session *s,
                        const uint8_t *source, uint32_t bytes) {
    if (!s || !s->ready || !s->ftdi_mode ||
        s->hid_mode || s->dual_mode || s->io_active ||
        !source || bytes == 0U || bytes > XHCI_FTDI_PACKET_BYTES)
        return -EINVAL;
    s->io_active = true;
    s->debug_stage = NAS_XHCI_STAGE_FTDI_BULK_OUT;
    copy_bytes(page_cpu(s, XHCI_P_PAYLOAD), source, bytes);
    int rc = dma_sync(s, XHCI_P_PAYLOAD, 0U, bytes, NAS_DMA_TO_DEVICE);
    uint64_t trb_bus = 0U;
    if (!rc) {
        struct xhci_trb trb;
        zero_bytes(&trb, sizeof(trb));
        trb.parameter = page_bus(s, XHCI_P_PAYLOAD);
        trb.status = bytes;
        trb.control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;
        rc = ring_enqueue(s, 2U, &trb, &trb_bus);
    }
    if (!rc) rc = ring_doorbell(s, s->slot_id, s->bulk_out_dci);
    if (!rc) {
        rc = -ETIMEDOUT;
        for (uint32_t tick = 0U; tick < XHCI_POLL_LIMIT; ++tick) {
            struct xhci_trb event;
            int next = event_next(s, &event);
            if (next == -EAGAIN) continue;
            if (next) { rc = next; break; }
            uint32_t kind = (event.control >> 10) & 63U;
            if (kind == XHCI_EVENT_PORT_STATUS) {
                next = ftdi_event_port(s);
                if (next) { rc = next; break; }
                continue;
            }
            if (kind != XHCI_EVENT_TRANSFER ||
                ((event.control >> 24) & 255U) != s->slot_id) {
                rc = -EIO;
                break;
            }
            if (((event.control >> 16) & 31U) == s->bulk_in_dci) {
                next = ftdi_receive_event(s, &event);
                if (next) { rc = next; break; }
                continue;
            }
            if ((event.parameter & ~0xFULL) != trb_bus ||
                ((event.control >> 16) & 31U) != s->bulk_out_dci) {
                rc = -EIO;
                break;
            }
            uint32_t code = (event.status >> 24) & 255U;
            uint32_t residual = event.status & 0xffffffU;
            s->debug_completion_code = code;
            s->debug_event_type = kind;
            rc = code != XHCI_CC_SUCCESS ? -EIO :
                 residual != 0U ? -EMSGSIZE : 0;
            break;
        }
    }
    s->io_active = false;
    return rc;
}

int nas_xhci_ftdi_close(struct nas_xhci_ro_session *s) {
    if (!s || !s->ftdi_mode) return -EINVAL;
    /* H11's close performs the verified HCHalted gate, then H6 DMA
     * disarm/lease free and zeroes the complete session. */
    return nas_xhci_ro_close(s);
}
