/* End-to-end host test of PRODUCTION DMA+GEM+VirtIO queue adapters.
 * Hardware transport and GEM handle lookup are the ONLY emulated interfaces.
 * This is not a VirGL renderer or a real-QEMU graphics acceptance test. */
#include "../kernel/drivers/port/dma.h"
#include "../kernel/fs/drm_gem.h"
#include "../kernel/errno.h"
#include "../ports/unikraft/split_queue.h"
#include "../ports/linux-virtgpu/virtqueue_bridge.h"
#include "../ports/linux-virtgpu/gem_dma_port.h"
#include <stdint.h>
#include <stdbool.h>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
#define GEM_BASE UINT64_C(0x10000000)
static unsigned frames[4]={0U,1U,1U,1U}, gem_refs, kicks, resets, fails;
static unsigned char queue_storage[4096] __attribute__((aligned(4096)));
uint64_t nas_frame_alloc(void) {return 0x3000U;}
uint64_t nas_hhdm_offset(void) {
    return (uint64_t)(uintptr_t)queue_storage-0x3000U;
}
bool nas_frame_get(uint64_t p) {
    unsigned n=(unsigned)(p/4096U);
    if(!p || p%4096U || n>3U || !frames[n] || frames[n]==UINT32_MAX)
        return false;
    ++frames[n];return true;
}
bool nas_frame_put(uint64_t p) {
    unsigned n=(unsigned)(p/4096U);
    if(!p || p%4096U || n>3U || !frames[n])return false;
    --frames[n];return true;
}
static void gem_put(void *ctx) {(void)ctx;--gem_refs;}
static int gem_frame(void *ctx,uint64_t offset,uint64_t *frame) {
    (void)ctx;
    if(offset!=GEM_BASE || !frame)return -EINVAL;
    *frame=0x1000U;return 0;
}
static struct nas_vm_device owner={
    .context=&gem_refs,.extent_bytes=GEM_BASE+4096U,
    .put=gem_put,.page=gem_frame
};
int nas_drm_gem_map_offset(unsigned slot,uint32_t handle,uint64_t *out) {
    if(slot!=5U || handle!=7U || !out)return -ENOENT;
    *out=GEM_BASE;return 0;
}
int nas_drm_gem_mmap_pin(unsigned slot,uint64_t at,struct nas_vm_device **out) {
    if(slot!=5U || at!=GEM_BASE || !out)return -EINVAL;
    ++gem_refs;*out=&owner;return 0;
}
int nas_virtio_setup_queue(const struct nas_virtio_transport *t,unsigned id,
                           unsigned count,uint64_t d,uint64_t a,uint64_t u) {
    (void)t;
    return id==0U && count==16U && d && a && u ? 0:-EINVAL;
}
void nas_virtio_notify(const struct nas_virtio_transport *t,unsigned id) {
    (void)t;(void)id;++kicks;
}
void nas_virtio_fail(const struct nas_virtio_transport *t) {
    (void)t;++fails;
}
static void transport_reset(void *ctx) {(void)ctx;++resets;}

int main(void) {
    const struct nas_virtio_transport_ops ops={.reset=transport_reset};
    const struct nas_virtio_transport t={
        .context=&kicks,.dma=&nas_dma_qemu_coherent,.ops=&ops};
    struct nas_vring_queue q={0};
    struct nas_linux_virtgpu_vq_bridge bridge={0};
    struct nas_linux_virtgpu_gem_page gem={0};
    int token=42; void *cookie=NULL;uint32_t used=0;
    CHECK(nas_vring_queue_init(&q,&t,0,16U)==0);
    CHECK(nas_linux_virtgpu_vq_bind(&bridge,&q)==0);
    CHECK(nas_linux_virtgpu_gem_map_page(5U,7U,0U,t.dma,&gem)==0);
    CHECK(gem.owner==&owner && gem_refs==1U && frames[1]==2U);
    CHECK(gem.dma.bus_address==0x1000U);
    struct nas_linux_virtgpu_dma_sg sg={
        .map=&gem.dma,.offset=64U,.length=32U,.device_writes=false};
    CHECK(nas_linux_virtgpu_vq_submit_mapped(&bridge,&sg,1U,&token)==0);
    uint16_t head=q.ring.avail->ring[0];
    CHECK(q.ring.desc[head].addr==0x1040U);
    CHECK(q.ring.desc[head].len==32U && q.inflight==1U);
    CHECK(gem_refs==1U && frames[1]==2U && kicks==0U);
    CHECK(nas_linux_virtgpu_vq_kick_prepare(&bridge));
    nas_linux_virtgpu_vq_notify(&bridge);
    CHECK(kicks==1U);
    /* The device consumes exactly the mapped GEM page and publishes USED. */
    q.ring.used->ring[0].id=head;
    q.ring.used->ring[0].len=0U;
    q.ring.used->idx=1U;
    CHECK(nas_linux_virtgpu_vq_reclaim(&bridge,&cookie,&used)==0);
    CHECK(cookie==&token && q.inflight==0U);
    CHECK(nas_linux_virtgpu_gem_unmap_page(&gem)==0);
    CHECK(gem_refs==0U && frames[1]==1U);
    CHECK(nas_vring_queue_close(&q)==0);
    CHECK(frames[3]==0U && resets==1U && fails==0U);
    return 0;
}
