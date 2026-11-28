/* Host check of the REAL kernel DMA borrowed-frame implementation.
 * Fake only the physical frame allocator; do not fake DMA address mapping
 * or the reference-counted map/unmap functions under test. */
#include "../kernel/drivers/port/dma.h"
#include "../ports/linux-virtgpu/gem_dma_port.h"
#include "../kernel/errno.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CHECK(expr) do { if (!(expr))return __LINE__; } while (0)
#define FRAME_A UINT64_C(0x1000)
#define FRAME_B UINT64_C(0x2000)

static unsigned refs_a=1U, refs_b=1U, fake_map_calls, fake_unmap_calls;
static bool fail_unmap;
static uint64_t fake_bus;
static unsigned char host_dma_memory[NAS_DMA_PAGE_BYTES]
    __attribute__((aligned(4096)));

uint64_t nas_frame_alloc(void) {return FRAME_A;}
uint64_t nas_hhdm_offset(void) {
    return (uint64_t)(uintptr_t)host_dma_memory-FRAME_A;
}
bool nas_frame_get(uint64_t frame) {
    if(frame==FRAME_A && refs_a && refs_a<UINT32_MAX){refs_a++;return true;}
    if(frame==FRAME_B && refs_b && refs_b<UINT32_MAX){refs_b++;return true;}
    return false;
}
bool nas_frame_put(uint64_t frame) {
    if(frame==FRAME_A && refs_a){refs_a--;return true;}
    if(frame==FRAME_B && refs_b){refs_b--;return true;}
    return false;
}
/* The synthetic IOMMU provider below models a successful device mapping
 * that can fail to tear down. It always pins each physical frame first. */
static int synthetic_map(void *ctx,uint64_t frame,uint64_t *bus) {
    (void)ctx;
    if(!nas_frame_get(frame))return -EINVAL;
    fake_map_calls++;
    *bus=fake_bus;
    return 0;
}
static int synthetic_unmap(void *ctx,uint64_t frame,uint64_t bus) {
    (void)ctx;
    if(bus!=fake_bus || fail_unmap)return -EIO;
    fake_unmap_calls++;
    return nas_frame_put(frame)?0:-EIO;
}
/* Stub only the pre-existing DRM GEM handle lookup and VMA owner.
 * Run the actual GEM->DMA bridge and production DMA page mapper. */
#define GEM_BASE UINT64_C(0x10000000)
static unsigned gem_pins;
static void gem_put(void *ctx) {(void)ctx;gem_pins--;}
static int gem_page(void *ctx,uint64_t at,uint64_t *frame) {
    (void)ctx;
    if(!frame)return -EINVAL;
    if(at==GEM_BASE){*frame=FRAME_A;return 0;}
    if(at==GEM_BASE+NAS_DMA_PAGE_BYTES){*frame=FRAME_B;return 0;}
    return -EINVAL;
}
static struct nas_vm_device gem_owner={
    .context=&gem_pins,.extent_bytes=GEM_BASE+2*NAS_DMA_PAGE_BYTES,
    .put=gem_put,.page=gem_page
};
int nas_drm_gem_map_offset(unsigned slot,uint32_t handle,uint64_t *out) {
    if(!out || slot!=5U || handle!=7U)return -ENOENT;
    *out=GEM_BASE;
    return 0;
}
int nas_drm_gem_mmap_pin(unsigned slot,uint64_t at,struct nas_vm_device **out) {
    if(!out || slot!=5U || at!=GEM_BASE)return -EINVAL;
    gem_pins++;
    *out=&gem_owner;
    return 0;
}
static int test_gem_dma(void) {
    struct nas_linux_virtgpu_gem_page pin={0};
    CHECK(nas_linux_virtgpu_gem_map_page(5U,7U,1U,
          &nas_dma_qemu_coherent,&pin)==0);
    CHECK(pin.owner==&gem_owner && pin.dma.frame_pa==FRAME_B);
    CHECK(gem_pins==1U && refs_b==2U);
    CHECK(nas_linux_virtgpu_gem_map_page(5U,7U,0U,
          &nas_dma_qemu_coherent,&pin)==-EBUSY);
    CHECK(nas_linux_virtgpu_gem_unmap_page(&pin)==0);
    CHECK(gem_pins==0U && refs_b==1U);
    CHECK(nas_linux_virtgpu_gem_unmap_page(&pin)==-EINVAL);
    CHECK(nas_linux_virtgpu_gem_map_page(5U,7U,3U,
          &nas_dma_qemu_coherent,&pin)==-EINVAL);
    CHECK(gem_pins==0U && refs_a==1U && refs_b==1U);
    CHECK(nas_linux_virtgpu_gem_map_page(5U,7U,0U,
          &nas_dma_qemu_coherent,&pin)==0);
    CHECK(gem_pins==1U && refs_a==2U);
    CHECK(nas_linux_virtgpu_gem_unmap_page(&pin)==0);
    CHECK(gem_pins==0U && refs_a==1U);
    return 0;
}
static int test_mapping(void) {
    struct nas_dma_mapped_frame map={0}, second={0};
    uint64_t bus=UINT64_MAX;
    CHECK(nas_dma_map_borrowed_frame(&nas_dma_qemu_coherent,FRAME_A,&map)==0);
    CHECK(map.active && map.provider==&nas_dma_qemu_coherent);
    CHECK(refs_a==2U && map.frame_pa==FRAME_A && map.bus_address==FRAME_A);
    CHECK(nas_dma_mapped_slice(&map,128U,64U,&bus)==0 && bus==FRAME_A+128U);
    CHECK(nas_dma_mapped_slice(&map,4095U,2U,&bus)==-EINVAL);
    CHECK(nas_dma_mapped_slice(&map,0U,0U,&bus)==-EINVAL);
    CHECK(nas_dma_map_borrowed_frame(&nas_dma_qemu_coherent,FRAME_B,&second)==0);
    CHECK(refs_b==2U);
    CHECK(nas_dma_map_borrowed_frame(&nas_dma_qemu_coherent,FRAME_A,&map)==-EBUSY);
    CHECK(nas_dma_unmap_borrowed_frame(&map)==0 && refs_a==1U);
    CHECK(nas_dma_unmap_borrowed_frame(&map)==-EINVAL);
    CHECK(nas_dma_unmap_borrowed_frame(&second)==0 && refs_b==1U);
    CHECK(nas_dma_map_borrowed_frame(&nas_dma_qemu_coherent,FRAME_A+1U,&map)==-EINVAL);
    CHECK(nas_dma_map_borrowed_frame(&nas_dma_qemu_coherent,0x3000U,&map)==-EINVAL);
    CHECK(refs_a==1U && refs_b==1U);

    const struct nas_dma_provider fake={
        .coherent=true,.map_frame=synthetic_map,.unmap_frame=synthetic_unmap};
    const struct nas_dma_provider incoherent={
        .coherent=false,.map_frame=synthetic_map,.unmap_frame=synthetic_unmap};
    CHECK(nas_dma_map_borrowed_frame(&incoherent,FRAME_A,&map)==-EOPNOTSUPP);
    CHECK(nas_dma_map_borrowed_frame(&(struct nas_dma_provider){.coherent=true},
                                     FRAME_A,&map)==-EOPNOTSUPP);
    CHECK(refs_a==1U);

    fake_bus=UINT64_C(0x7654321000);
    CHECK(nas_dma_map_borrowed_frame(&fake,FRAME_A,&map)==0);
    CHECK(map.bus_address==fake_bus && refs_a==2U);
    CHECK(nas_dma_mapped_slice(&map,4095U,1U,&bus)==0 &&
          bus==fake_bus+4095U);
    fail_unmap=true;
    CHECK(nas_dma_unmap_borrowed_frame(&map)==-EIO);
    CHECK(map.active && refs_a==2U);
    fail_unmap=false;
    CHECK(nas_dma_unmap_borrowed_frame(&map)==0 && refs_a==1U);

    /* Impossible bus-page address must be rejected and its pin released. */
    fake_bus=UINT64_MAX-15U;
    CHECK(nas_dma_map_borrowed_frame(&fake,FRAME_A,&map)==-EOVERFLOW);
    CHECK(!map.active && refs_a==1U);
    /* When automatic unwind itself fails, retain ownership for retry. */
    fail_unmap=true;
    CHECK(nas_dma_map_borrowed_frame(&fake,FRAME_A,&map)==-EIO);
    CHECK(map.active && refs_a==2U);
    fail_unmap=false;
    CHECK(nas_dma_unmap_borrowed_frame(&map)==0 && refs_a==1U);
    CHECK(fake_map_calls==3U && fake_unmap_calls==3U);
    return 0;
}
int main(void) {
    int rc=test_mapping();
    if(rc)return rc;
    return test_gem_dma();
}
