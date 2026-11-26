#include "firmware_fdt.h"
#include <libfdt.h>

struct nas_fdt_hardware nas_fdt_hw;
#define MAX_VIRTIO 32U
#define PAGE 4096U

static bool cell(const void *fdt, int node, const char *name, uint32_t *out) {
    int len;
    const fdt32_t *p = fdt_getprop(fdt, node, name, &len);
    if (!p || len != 4) return false;
    *out = fdt32_ld(p);
    return true;
}
static bool enabled(const void *fdt, int node) {
    int len;
    const char *s = fdt_getprop(fdt, node, "status", &len);
    return !s ? len == -FDT_ERR_NOTFOUND :
        (len == 5 && !memcmp(s, "okay", 5)) ||
        (len == 3 && !memcmp(s, "ok", 3));
}
static int compatible(const void *fdt, const char *name) {
    int node = -1;
    while ((node = fdt_node_offset_by_compatible(fdt, node, name)) >= 0)
        if (enabled(fdt, node)) return node;
    return node;
}
static uint64_t number(const fdt32_t *p, unsigned count) {
    uint64_t n = 0;
    while (count--) n = (n << 32) | fdt32_ld(p++);
    return n;
}
/* Existing drivers consume CPU physical ranges. Accept root children and
 * identity-mapped simple buses, not untranslated bus-local addresses. */
static bool reg(const void *fdt, int node, unsigned index,
                uint64_t *base, uint64_t *size) {
    int parent = fdt_parent_offset(fdt, node);
    if (parent < 0) return false;
    for (int bus = parent; bus > 0; bus = fdt_parent_offset(fdt, bus)) {
        int len;
        if (!fdt_getprop(fdt, bus, "ranges", &len) || len != 0)
            return false;
    }
    uint32_t ac = 2, sc = 1;
    (void)cell(fdt, parent, "#address-cells", &ac);
    (void)cell(fdt, parent, "#size-cells", &sc);
    if (ac < 1 || ac > 2 || sc < 1 || sc > 2) return false;
    int len;
    const fdt32_t *p = fdt_getprop(fdt, node, "reg", &len);
    unsigned step = ac + sc;
    if (!p || len < 0 || (unsigned)len % (step * 4U) ||
        index >= (unsigned)len / (step * 4U)) return false;
    p += index * step;
    *base = number(p, ac);
    *size = number(p + ac, sc);
    return *base && *size && *base <= UINT64_MAX - *size;
}
static int irq_parent(const void *fdt, int node) {
    for (int n = node; n >= 0; n = fdt_parent_offset(fdt, n)) {
        uint32_t phandle;
        if (cell(fdt, n, "interrupt-parent", &phandle))
            return fdt_node_offset_by_phandle(fdt, phandle);
    }
    return -1;
}
static bool irq(const void *fdt, int node, int controller, bool rv,
                unsigned index, bool timer, unsigned *id, bool *edge) {
    uint32_t cells;
    if (irq_parent(fdt, node) != controller ||
        !cell(fdt, controller, "#interrupt-cells", &cells) ||
        cells != (rv ? 1U : 3U)) return false;
    int len;
    const fdt32_t *p = fdt_getprop(fdt, node, "interrupts", &len);
    if (!p || len < 0 || (unsigned)len % (cells * 4U) ||
        index >= (unsigned)len / (cells * 4U)) return false;
    p += index * cells;
    if (rv) {
        *id = fdt32_ld(p);
        *edge = false;
        return *id > 0 && *id < 32;
    }
    uint32_t type = fdt32_ld(p), n = fdt32_ld(p + 1);
    uint32_t flags = fdt32_ld(p + 2) & 15U;
    if (timer) {
        if (type != 1 || n >= 16) return false;
        *id = n + 16U;
    } else {
        if (type != 0 || n >= 988 || (flags != 1 && flags != 4)) return false;
        *id = n + 32U;
    }
    *edge = flags == 1U;
    return true;
}
static bool plic_setup(const void *fdt, int controller, uint64_t hart,
                       struct nas_fdt_hardware *h) {
    uint64_t size;
    if (!reg(fdt, controller, 0, &h->plic, &size) ||
        (h->plic & (PAGE - 1U)) || size < PAGE) return false;
    int len;
    const fdt32_t *p = fdt_getprop(fdt, controller, "interrupts-extended", &len);
    if (!p || len <= 0 || len % 8) return false;
    for (unsigned i = 0; i < (unsigned)len / 8U; ++i) {
        int intc = fdt_node_offset_by_phandle(fdt, fdt32_ld(p + i * 2));
        uint32_t cells;
        if (intc < 0 || !cell(fdt, intc, "#interrupt-cells", &cells) ||
            cells != 1U) return false;
        if (fdt32_ld(p + i * 2 + 1) != 9U) continue;
        int cpu = fdt_parent_offset(fdt, intc);
        int cpus = fdt_parent_offset(fdt, cpu);
        uint32_t ac;
        int bytes;
        const fdt32_t *id = fdt_getprop(fdt, cpu, "reg", &bytes);
        if (!cell(fdt, cpus, "#address-cells", &ac) || ac < 1 || ac > 2 ||
            !id || bytes != (int)(ac * 4U)) return false;
        if (number(id, ac) != hart) continue;
        uint64_t en = UINT64_C(0x2000) + (uint64_t)i * 0x80U;
        uint64_t ctx = UINT64_C(0x200000) + (uint64_t)i * PAGE;
        if (en >= size || size - en < 4U || ctx >= size || size - ctx < PAGE)
            return false;
        h->plic_enable = h->plic + en;
        h->plic_context = h->plic + ctx;
        return true;
    }
    return false;
}
bool nas_fdt_discover(const void *fdt, size_t extent, bool rv, uint64_t hart) {
    nas_fdt_hw = (struct nas_fdt_hardware){0};
    if (!fdt || extent < sizeof(struct fdt_header) ||
        extent > UINT32_MAX || fdt_check_full(fdt, extent)) return false;
    struct nas_fdt_hardware h = {0};
    int controller;
    if (rv) {
        uint32_t hz;
        int cpus = fdt_path_offset(fdt, "/cpus");
        if (cpus < 0 || !cell(fdt, cpus, "timebase-frequency", &hz) ||
            hz < 100U) return false;
        h.timebase_hz = hz;
        controller = compatible(fdt, "riscv,plic0");
        if (controller < 0) controller = compatible(fdt, "sifive,plic-1.0.0");
        if (controller < 0 || !plic_setup(fdt, controller, hart, &h))
            return false;
    } else {
        controller = compatible(fdt, "arm,cortex-a15-gic");
        if (controller < 0) controller = compatible(fdt, "arm,gic-400");
        uint64_t dsize, csize;
        if (controller < 0 || !reg(fdt, controller, 0, &h.gicd, &dsize) ||
            !reg(fdt, controller, 1, &h.gicc, &csize) ||
            ((h.gicd | h.gicc) & (PAGE - 1U)) ||
            dsize < PAGE || csize < PAGE) return false;
        int timer = compatible(fdt, "arm,armv8-timer");
        bool edge;
        if (timer < 0 || !irq(fdt, timer, controller, false, 2, true,
                              &h.timer_irq, &edge)) return false;
    }

    /* The existing transport scanner accepts a contiguous, uniform MMIO
     * aperture. Derive it from DT nodes in address order, never node order.
     * Reject nonrepresentable layouts instead of probing guessed MMIO. */
    struct slot { uint64_t base, size; unsigned irq; bool edge; } slots[MAX_VIRTIO];
    unsigned count = 0;
    int node = -1;
    while ((node = fdt_node_offset_by_compatible(fdt, node, "virtio,mmio")) >= 0) {
        if (!enabled(fdt, node)) continue;
        struct slot s;
        if (count == MAX_VIRTIO || !reg(fdt, node, 0, &s.base, &s.size) ||
            !irq(fdt, node, controller, rv, 0, false, &s.irq, &s.edge) ||
            s.size < 0x200U || s.size > PAGE || (s.size & 3U)) return false;
        unsigned i = count++;
        while (i && slots[i - 1].base > s.base) {
            slots[i] = slots[i - 1]; --i;
        }
        slots[i] = s;
    }
    if (node != -FDT_ERR_NOTFOUND) return false;
    if (count) {
        h.virtio_base = slots[0].base;
        h.virtio_stride = (unsigned)slots[0].size;
        h.virtio_irq_first = slots[0].irq;
        if (h.virtio_base & (PAGE - 1U)) return false;
        for (unsigned i = 0; i < count; ++i) {
            uint64_t offset = (uint64_t)i * h.virtio_stride;
            if (h.virtio_base > UINT64_MAX - offset ||
                slots[i].base != h.virtio_base + offset ||
                slots[i].size != h.virtio_stride ||
                slots[i].irq != h.virtio_irq_first + i) return false;
            if (slots[i].edge) h.virtio_edge_mask |= UINT32_C(1) << i;
        }
        h.virtio_count = count;
    }
    nas_fdt_hw = h;
    return true;
}
