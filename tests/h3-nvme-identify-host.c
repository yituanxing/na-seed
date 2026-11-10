#include "../kernel/drivers/nvme/identify.h"
#include "../kernel/errno.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-memory stand-in for H3's existing QEMU coherent frame provider. The
 * protocol sees bus addresses, never a host-only pointer passed as an API. */
static void *frames[8];
static unsigned allocated, released;
static bool controller_dma_live, unsafe_release_attempted;
uint64_t nas_frame_alloc(void) {
    if (allocated >= 8U) return 0;
    void *page = aligned_alloc(NAS_DMA_PAGE_BYTES, NAS_DMA_PAGE_BYTES);
    if (!page) return 0;
    memset(page, 0, NAS_DMA_PAGE_BYTES);
    frames[allocated++] = page;
    return (uint64_t)(uintptr_t)page;
}
uint64_t nas_hhdm_offset(void) { return 0; }
bool nas_frame_put(uint64_t addr) {
    if (controller_dma_live) {
        unsafe_release_attempted = true;
        return false;
    }
    for (unsigned i = 0; i < allocated; ++i) {
        if (frames[i] == (void *)(uintptr_t)addr) {
            free(frames[i]);
            frames[i] = NULL;
            ++released;
            return true;
        }
    }
    return false;
}

struct fake_controller {
    uint32_t regs[0x2000 / 4];
    unsigned sq_doorbell, cq_doorbell;
    bool suppress_completion;
    bool complete_with_error;
    bool fail_disable;
    bool command_valid;
};

static void store32(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)x;
    p[1] = (uint8_t)(x >> 8);
    p[2] = (uint8_t)(x >> 16);
    p[3] = (uint8_t)(x >> 24);
}
static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get_address(const struct fake_controller *f, unsigned offset) {
    return (uint64_t)f->regs[offset / 4U] |
        ((uint64_t)f->regs[offset / 4U + 1U] << 32);
}

static int fake_read(void *context, uint32_t off, uint32_t *value) {
    struct fake_controller *f = context;
    if (off >= sizeof(f->regs) || (off & 3U)) return -EIO;
    *value = f->regs[off / 4U];
    return 0;
}
static void fake_identify(struct fake_controller *f) {
    if (f->suppress_completion) return;
    uint8_t *sq = (uint8_t *)(uintptr_t)get_address(f, 0x28U);
    uint8_t *cq = (uint8_t *)(uintptr_t)get_address(f, 0x30U);
    const uint64_t prp1 = (uint64_t)load32(sq + 24U) |
                          ((uint64_t)load32(sq + 28U) << 32);
    uint8_t *id = (uint8_t *)(uintptr_t)prp1;
    f->command_valid = load32(sq) == (6U | (1U << 16)) &&
        load32(sq + 40U) == 1U &&
        f->regs[0x24U / 4U] == 0x00010001U &&
        f->regs[0x14U / 4U] == (1U | (6U << 16) | (4U << 20));
    if (!f->command_valid) return;
    id[0] = 0x86U;
    id[1] = 0x80U;
    memcpy(id + 4U, "H3-FAKE-SERIAL      ", 20U);
    memcpy(id + 24U, "QEMU FIXTURE NVME                       ", 40U);
    store32(id + 516U, 0U); /* Controller with no attached namespace. */
    store32(cq + 8U, 1U);   /* SQ head=1, SQ ID=0. */
    store32(cq + 12U, 1U | (1U << 16) |
            (f->complete_with_error ? (2U << 17) : 0U));
}
static int fake_write(void *context, uint32_t off, uint32_t value) {
    struct fake_controller *f = context;
    if (off >= sizeof(f->regs) || (off & 3U)) return -EIO;
    f->regs[off / 4U] = value;
    if (off == 0x14U) {
        if (value & 1U) f->regs[0x1cU / 4U] = 1U;
        else if (!f->fail_disable) f->regs[0x1cU / 4U] = 0U;
        controller_dma_live = (f->regs[0x1cU / 4U] & 1U) != 0U;
    }
    if (off == 0x1000U) {
        ++f->sq_doorbell;
        if (value == 1U) fake_identify(f);
    }
    if (off == 0x1004U && value == 1U) ++f->cq_doorbell;
    return 0;
}

static void reset(struct fake_controller *f) {
    for (unsigned i = 0; i < allocated; ++i) {
        if (frames[i]) free(frames[i]);
        frames[i] = NULL;
    }
    allocated = released = 0U;
    controller_dma_live = unsafe_release_attempted = false;
    memset(f, 0, sizeof(*f));
    f->regs[0] = 63U;
    f->regs[1] = 1U << 5; /* CAP.CSS.NVM bit 37. */
    f->regs[2] = 0x00010400U;
}
static int run(struct fake_controller *f, uint32_t span,
               struct nas_nvme_identity *out) {
    const struct nas_nvme_io io = {
        .context = f, .mmio_bytes = span,
        .read32 = fake_read, .write32 = fake_write
    };
    return nas_nvme_identify_controller(&io, &nas_dma_qemu_coherent, out);
}
#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "H3 FAIL at line %u: %s\n", __LINE__, #test); \
    return 1; } } while (0)

int main(void) {
    struct fake_controller f;
    struct nas_nvme_identity id;
    reset(&f);
    CHECK(run(&f, 0x2000U, &id) == 0);
    CHECK(f.command_valid && f.sq_doorbell == 1U && f.cq_doorbell == 1U);
    CHECK(f.regs[0x1cU / 4U] == 0U);
    CHECK(id.version == 0x00010400U && id.vendor_id == 0x8086U);
    CHECK(id.controller_nn == 0U);
    CHECK(strcmp(id.serial, "H3-FAKE-SERIAL") == 0);
    CHECK(strcmp(id.model, "QEMU FIXTURE NVME") == 0);
    CHECK(allocated == 3U && released == 3U);
    CHECK(!unsafe_release_attempted);

    reset(&f);
    f.complete_with_error = true;
    CHECK(run(&f, 0x2000U, &id) == -EIO);
    CHECK(allocated == 3U && released == 3U);
    CHECK(!unsafe_release_attempted);

    reset(&f);
    f.suppress_completion = true;
    CHECK(run(&f, 0x2000U, &id) == -ETIMEDOUT);
    CHECK(allocated == 3U && released == 3U);
    CHECK(!unsafe_release_attempted);

    reset(&f);
    f.fail_disable = true;
    CHECK(run(&f, 0x2000U, &id) == -ETIMEDOUT);
    CHECK(allocated == 3U && released == 0U); /* Pin all unsafe pages. */
    CHECK(!unsafe_release_attempted);

    reset(&f);
    f.regs[1] |= 1U << 16; /* Minimum memory page size > 4KiB. */
    CHECK(run(&f, 0x2000U, &id) == -EOPNOTSUPP);
    CHECK(allocated == 0U);

    reset(&f);
    f.regs[1] |= 1U; /* CAP.DSTRD=1, CQ doorbell at 0x1008. */
    CHECK(run(&f, 0x1008U, &id) == -EOPNOTSUPP);
    CHECK(allocated == 0U);

    reset(&f);
    CHECK(run(&f, 0x1004U, &id) == -EINVAL);
    CHECK(allocated == 0U);

    puts("H3 NVMe Identify host: PASS (QEMU-like completion, error, timeout, DMA pinning, CAP bounds)");
    return 0;
}
