#include "xhci.h"
#include "../../errno.h"
#include <stddef.h>

/*
 * QEMU-only xHCI USB mass-storage controller transport.
 *
 * The Intel xHCI 1.2 specification defines the MMIO and TRB structures.
 * QEMU hw/usb/hcd-xhci.c is used only as the test-device implementation:
 * no operating-system USB stack, IRQ handler, hub topology or hotplug is
 * emulated by this driver.
 *
 * We intentionally support one root-port BOT interface and one outstanding
 * command/transfer. Every ring/payload DMA address belongs to a H6 lease.
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
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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
static int dma_sync(struct nas_xhci_ro_session *s, unsigned page,
                    uint32_t off, uint32_t length,
                    enum nas_dma_direction direction) {
    if (page >= NAS_XHCI_RO_DMA_PAGES) return -EINVAL;
    return nas_dma_lease_sync(&s->lease, &s->pages[page], off,
                              length, direction);
}
static uint8_t *page_cpu(struct nas_xhci_ro_session *s, unsigned page) {
    return (uint8_t *)s->pages[page].cpu;
}
static uint64_t page_bus(struct nas_xhci_ro_session *s, unsigned page) {
    return s->pages[page].bus_address;
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
    if (!rc) rc = await_event(s, XHCI_EVENT_COMMAND,
                              trb_bus, 0, slot_out, NULL);
    return rc;
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
static int enable_port(struct nas_xhci_ro_session *s, uint32_t max_ports) {
    if (!max_ports || max_ports > 15U) return -ENODEV;
    for (uint32_t p = 1; p <= max_ports; ++p) {
        uint32_t off = s->op_base + XHCI_OP_PORTSC +
                       (p - 1U) * XHCI_PORT_STRIDE;
        uint32_t v;
        if (io_read(s, off, &v)) return -EIO;
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
static int setup_context(struct nas_xhci_ro_session *s) {
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
    uint8_t device[18];
    int rc = control_request(s, 0x80, 6, 0x100, 0, sizeof(device), device);
    if (rc || device[0] < 18U || device[1] != 1U) return -ENODEV;
    uint8_t first_config[9];
    rc = control_request(s, 0x80, 6, 0x200, 0,
                         sizeof(first_config), first_config);
    if (rc || first_config[0] < 9U || first_config[1] != 2U)
        return -ENODEV;
    uint16_t total = le16(&first_config[2]);
    if (total < 18U || total > 512U) return -E2BIG;
    uint8_t config[512];
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
static int run_controller(struct nas_xhci_ro_session *s) {
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
        max_slots < 1U || max_ports < 1U || max_ports > 15U ||
        scratchpad != 0U || !(hcc & 1U))
        return -ENODEV;
    if (quiesce(s)) return -EIO;
    if (op_write(s, XHCI_OP_USBCMD, XHCI_CMD_HCRST)) return -EIO;
    if (poll_bits(s, s->op_base + XHCI_OP_USBCMD,
                  XHCI_CMD_HCRST, 0) ||
        poll_bits(s, s->op_base + XHCI_OP_USBSTS,
                  XHCI_STS_CNR, 0)) return -ETIMEDOUT;
    if (op_read(s, XHCI_OP_PAGESIZE, &pagesize) ||
        !(pagesize & 1U)) return -ENODEV;
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
        op_write(s, XHCI_OP_CONFIG, 1U))
        return -EIO;
    /* Arm H6 lease BEFORE RUN enables controller fetches and bus DMA. */
    if (nas_dma_lease_arm(&s->lease)) return -EIO;
    s->may_dma = true;
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
    s->ready = true;
    return 0;
}
static int bulk_transfer(struct nas_xhci_ro_session *s, bool in,
                         void *buffer, uint32_t bytes) {
    if (!s || !s->ready || s->io_active || !buffer ||
        !bytes || bytes > XHCI_PAYLOAD_MAX) return -EINVAL;
    uint8_t dci = in ? s->bulk_in_dci : s->bulk_out_dci;
    unsigned which = in ? 3U : 2U; /* EP ring IDs 2/3 */
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
    if (!rc) rc = await_event(s, XHCI_EVENT_TRANSFER, trb_bus,
                              dci, NULL, NULL);
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
    if (!s || !s->io || !s->dma || s->io_active) return -EINVAL;
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
