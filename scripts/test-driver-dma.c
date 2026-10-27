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
    puts("[DRIVER DMA PASS] identity/coherent page allocation, release, directional sync, bounds and noncoherent fail-closed");
    return 0;
}
