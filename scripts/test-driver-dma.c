#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "../kernel/drivers/port/dma.h"
#include "../kernel/errno.h"
static _Alignas(4096) unsigned char pages[4][4096];
static uint8_t inuse[4];
uint64_t nas_hhdm_offset(void) { return 0; }
uint64_t nas_frame_alloc(void) {
    for(unsigned i=0;i<4;i++) if(!inuse[i]) {
        inuse[i]=1;return (uint64_t)(uintptr_t)pages[i];
    }
    return 0;
}
bool nas_frame_put(uint64_t pa) {
    for(unsigned i=0;i<4;i++) if(pa==(uint64_t)(uintptr_t)pages[i]&&inuse[i]) {
        inuse[i]=0;return true;
    }
    return false;
}
static int fake_alloc(void *ctx, struct nas_dma_page *p){
    (void)ctx;(void)p;return -ENOMEM;
}
static int fake_free(void *ctx, struct nas_dma_page *p){
    (void)ctx;(void)p;return 0;
}
static int zero_dma_alloc(void *ctx, struct nas_dma_page *p){
    (void)ctx;
    p->cpu=pages[0];p->bus_address=0;p->bytes=NAS_DMA_PAGE_BYTES;
    return 0;
}
struct invalid_dma_state {unsigned allocations,releases;bool fail_release_once;};
static int invalid_dma_alloc(void *ctx, struct nas_dma_page *p){
    struct invalid_dma_state *state=ctx;
    ++state->allocations;
    p->cpu=pages[2];
    p->bus_address=(uint64_t)(uintptr_t)p->cpu;
    p->frame_pa=p->bus_address;
    p->bytes=NAS_DMA_PAGE_BYTES/2U; /* allocated, but unusable for DMA */
    return 0;
}
static int invalid_dma_free(void *ctx, struct nas_dma_page *p){
    struct invalid_dma_state *state=ctx;
    ++state->releases;
    assert(p->cpu==pages[2] && p->bytes==NAS_DMA_PAGE_BYTES/2U);
    if(state->fail_release_once){state->fail_release_once=false;return -EAGAIN;}
    return 0;
}
static int missing_cpu_alloc(void *ctx, struct nas_dma_page *p){
    (void)ctx;
    p->frame_pa=1234;p->bytes=NAS_DMA_PAGE_BYTES;
    return 0;
}
int main(void) {
    struct nas_dma_page q={0},r={0},f={0};
    assert(nas_dma_alloc_page(&nas_dma_qemu_coherent,&q)==0);
    assert(nas_dma_alloc_page(&nas_dma_qemu_coherent,&r)==0);
    assert(q.bus_address==(uint64_t)(uintptr_t)q.cpu && q.frame_pa==q.bus_address);
    assert(q.cpu!=r.cpu && q.bytes==4096);
    assert(nas_dma_sync(&nas_dma_qemu_coherent,&q,0,4096,NAS_DMA_TO_DEVICE)==0);
    assert(nas_dma_sync(&nas_dma_qemu_coherent,&q,4094,4,NAS_DMA_FROM_DEVICE)==-EINVAL);
    assert(nas_dma_sync(&nas_dma_qemu_coherent,&q,0,1,0)==-EINVAL);
    assert(nas_dma_free_page(&nas_dma_qemu_coherent,&q)==0);
    assert(q.cpu==NULL);
    assert(nas_dma_free_page(&nas_dma_qemu_coherent,&q)==-EINVAL);
    assert(nas_dma_free_page(&nas_dma_qemu_coherent,&r)==0);
    const struct nas_dma_provider not_coherent={
        .coherent=false,.alloc_page=fake_alloc,.release_page=fake_free
    };
    assert(nas_dma_alloc_page(&not_coherent,&f)==-EINVAL);
    const struct nas_dma_provider iommu_zero={
        .coherent=true,.alloc_page=zero_dma_alloc,.release_page=fake_free
    };
    assert(nas_dma_alloc_page(&iommu_zero,&f)==0);
    assert(f.bus_address==0 && f.cpu!=NULL);
    assert(nas_dma_free_page(&iommu_zero,&f)==0);
    struct invalid_dma_state state={.fail_release_once=true};
    const struct nas_dma_provider invalid_size={
        .context=&state,.coherent=true,
        .alloc_page=invalid_dma_alloc,.release_page=invalid_dma_free
    };
    assert(nas_dma_alloc_page(&invalid_size,&f)==-EAGAIN);
    assert(state.allocations==1 && state.releases==1);
    assert(f.cpu==pages[2] && f.bytes==NAS_DMA_PAGE_BYTES/2U);
    /* The failed release retains ownership but MUST NEVER make the
     * malformed descriptor usable for device DMA or new allocation. */
    assert(nas_dma_sync(&invalid_size,&f,0,8,NAS_DMA_TO_DEVICE)==-EINVAL);
    assert(nas_dma_alloc_page(&invalid_size,&f)==-EBUSY);
    assert(state.allocations==1);
    assert(nas_dma_free_page(&invalid_size,&f)==0);
    assert(state.releases==2 && f.cpu==NULL && f.bytes==0);
    assert(nas_dma_alloc_page(&invalid_size,&f)==-EIO);
    assert(state.allocations==2 && state.releases==3);
    assert(f.cpu==NULL && f.bytes==0);
    /* A provider claiming allocation success without a CPU mapping is
     * rejected; with a successful release, nothing remains owned. */
    const struct nas_dma_provider missing_cpu={
        .context=&state,.coherent=true,
        .alloc_page=missing_cpu_alloc,.release_page=fake_free
    };
    assert(nas_dma_alloc_page(&missing_cpu,&f)==-EIO);
    assert(f.cpu==NULL && f.bytes==0);
    puts("[DRIVER DMA PASS] coherent/zero-bus allocation, bounds, invalid-shape rollback, failed-release owner retry and fail-closed sync");
    return 0;
}
