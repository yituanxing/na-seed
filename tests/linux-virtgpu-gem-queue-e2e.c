/* End-to-end host test of PRODUCTION DMA+GEM+VirtIO queue adapters.
 * Hardware transport and GEM handle lookup are the ONLY emulated interfaces.
 * This models one-shot command payload DMA, NOT persistent GPU resource
 * backing (which must survive attach command completion until detach). */
#include "../kernel/drivers/port/dma.h"
#include "../kernel/fs/drm_gem.h"
#include "../kernel/errno.h"
#include "../ports/unikraft/split_queue.h"
#include "../ports/linux-virtgpu/virtqueue_bridge.h"
#include "../ports/linux-virtgpu/gem_dma_port.h"
#include "../ports/linux-virtgpu/vbuf_dma_port.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
#define GEM_BASE UINT64_C(0x10000000)
/* Real DMA allocation code under test receives three distinct emulated
 * PMM frames: ring, outgoing bounce and incoming bounce. */
static unsigned frames[6]={0U,1U,1U,0U,0U,0U}, gem_refs, kicks, resets, fails;
static unsigned next_frame=3U;
static unsigned char queue_storage[3U*4096U] __attribute__((aligned(4096)));
uint64_t nas_frame_alloc(void) {
    if(next_frame>5U)return 0U;
    unsigned n=next_frame++;
    frames[n]++;
    return (uint64_t)n*4096U;
}
uint64_t nas_hhdm_offset(void) {
    return (uint64_t)(uintptr_t)queue_storage-0x3000U;
}
bool nas_frame_get(uint64_t p) {
    unsigned n=(unsigned)(p/4096U);
    if(!p || p%4096U || n>5U || !frames[n] || frames[n]==UINT32_MAX)
        return false;
    ++frames[n];return true;
}
bool nas_frame_put(uint64_t p) {
    unsigned n=(unsigned)(p/4096U);
    if(!p || p%4096U || n>5U || !frames[n])return false;
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

    /* Feed a command allocated on an ordinary CPU stack through real
     * provider-owned DMA bounce memory and the Linux virtqueue API.
     * Only a true USED completion permits response copy-back + release. */
    struct nas_linux_virtgpu_virtqueue_port vport={0};
    CHECK(nas_linux_virtgpu_virtqueue_bind(&vport,&q,NULL,0U)==0);
    struct nas_linux_virtgpu_vbuf_dma cmd={0},resp={0};
    uint8_t command[32],response[32];
    for(unsigned i=0;i<32;i++) command[i]=(uint8_t)(0xa0U+i);
    memset(response,0x55,sizeof(response));
    CHECK(nas_linux_virtgpu_vbuf_dma_prepare(&cmd,t.dma,command,32U,false)==0);
    CHECK(nas_linux_virtgpu_vbuf_dma_prepare(&resp,t.dma,response,32U,true)==0);
    CHECK(frames[4]==2U && frames[5]==2U);
    CHECK(memcmp(command,cmd.page.cpu,32U)==0);
    CHECK(((uint8_t*)resp.page.cpu)[0]==0U && response[0]==0x55U);
    struct scatterlist sg_cmd,sg_resp;
    sg_init_table(&sg_cmd,1U);
    sg_init_table(&sg_resp,1U);
    CHECK(nas_linux_virtgpu_vbuf_dma_register_sg(&vport,&sg_cmd,&cmd)==0);
    CHECK(nas_linux_virtgpu_vbuf_dma_register_sg(&vport,&sg_resp,&resp)==0);
    struct scatterlist *lists[2]={&sg_cmd,&sg_resp};
    CHECK(virtqueue_add_sgs(&vport.linux_vq,lists,1U,1U,&token,0U)==0);
    uint16_t cmd_head=q.ring.avail->ring[1U];
    CHECK(q.ring.desc[cmd_head].addr==cmd.pin.bus_address);
    CHECK(q.ring.desc[q.ring.desc[cmd_head].next].addr==resp.pin.bus_address);
    CHECK(virtqueue_kick_prepare(&vport.linux_vq));
    CHECK(virtqueue_notify(&vport.linux_vq));
    CHECK(kicks==2U && frames[4]==2U && frames[5]==2U);
    /* Model the actual device DMA write to mapped response memory. */
    memset(resp.page.cpu,0xcc,32U);
    CHECK(response[0]==0x55U); /* No premature userspace copy-back. */
    q.ring.used->ring[1].id=cmd_head;
    q.ring.used->ring[1].len=32U;
    q.ring.used->idx=2U;
    unsigned int response_bytes=0;
    CHECK(virtqueue_get_buf(&vport.linux_vq,&response_bytes)==&token);
    CHECK(response_bytes==32U && vport.linux_vq.num_free==16U);
    CHECK(nas_linux_virtgpu_vbuf_dma_retire_after_quiesce(&resp,true)==0);
    CHECK(nas_linux_virtgpu_vbuf_dma_retire_after_quiesce(&cmd,true)==0);
    CHECK(response[0]==0xccU && response[31]==0xccU);
    CHECK(frames[4]==0U && frames[5]==0U);
    CHECK(nas_vring_queue_close(&q)==0);
    CHECK(frames[3]==0U && resets==1U && fails==0U);
    return 0;
}
