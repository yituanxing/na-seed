/* Host check of the REAL kernel DMA borrowed-frame implementation.
 * Fake only the physical frame allocator; do not fake DMA address mapping
 * or the reference-counted map/unmap functions under test. */
#include "../kernel/drivers/port/dma.h"
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
int main(void) {return test_mapping();}
