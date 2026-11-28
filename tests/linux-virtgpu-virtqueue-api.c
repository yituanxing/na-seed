/* Exercise Linux virtqueue_* signatures on the REAL Unikraft split-ring.
 * The only mocks are coherent DMA allocation and the hardware transport.
 * No synthetic VirtIO-GPU command processor or fake DRM render node. */
#include "../ports/linux-virtgpu/virtqueue_api_port.h"
#include "../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CHECK(x) do { if(!(x))return __LINE__; } while(0)
static unsigned char ring_memory[NAS_DMA_PAGE_BYTES]
    __attribute__((aligned(4096)));
static unsigned notices,resets,failed,releases;
int nas_dma_alloc_page(const struct nas_dma_provider *p,struct nas_dma_page *pg) {
    (void)p;
    *pg=(struct nas_dma_page){.cpu=ring_memory,.bus_address=0x30000,
                              .frame_pa=0x30000,.bytes=NAS_DMA_PAGE_BYTES};
    return 0;
}
int nas_dma_free_page(const struct nas_dma_provider *p,struct nas_dma_page *pg){
    (void)p;
    ++releases;
    *pg=(struct nas_dma_page){0};
    return 0;
}
int nas_dma_sync(const struct nas_dma_provider *p,struct nas_dma_page *pg,
                 uint32_t at,uint32_t bytes,enum nas_dma_direction d) {
    (void)p;(void)d;
    return pg && pg->cpu && at<=pg->bytes && bytes<=pg->bytes-at ? 0:-EIO;
}
int nas_virtio_setup_queue(const struct nas_virtio_transport *t,unsigned idx,
                           unsigned count,uint64_t desc,uint64_t avail,
                           uint64_t used) {
    (void)t;
    return idx==0 && count==16 && desc && avail && used ? 0:-EINVAL;
}
void nas_virtio_notify(const struct nas_virtio_transport *t,unsigned idx) {
    (void)t;(void)idx;notices++;
}
void nas_virtio_fail(const struct nas_virtio_transport *t) {
    (void)t;failed++;
}
static void device_reset(void *ctx) {(void)ctx;resets++;}

static int test(void) {
    const struct nas_dma_provider dma={.coherent=true};
    const struct nas_virtio_transport_ops ops={.reset=device_reset};
    const struct nas_virtio_transport t={
        .context=&notices,.dma=&dma,.ops=&ops};
    struct nas_vring_queue q={0};
    struct nas_linux_virtgpu_virtqueue_port port={0};
    struct nas_dma_mapped_frame cmd={
        .provider=&dma,.frame_pa=0x1000,.bus_address=0x50000,
        .active=true,.usable=true};
    struct nas_dma_mapped_frame response={
        .provider=&dma,.frame_pa=0x2000,.bus_address=0x60000,
        .active=true,.usable=true};
    struct scatterlist output[2],input[1];
    struct scatterlist *sgs[2]={output,input};
    int token=123;
    unsigned int len=555U;
    CHECK(nas_vring_queue_init(&q,&t,0,16)==0);
    CHECK(nas_linux_virtgpu_virtqueue_bind(&port,&q,NULL,0)==0);
    CHECK(port.linux_vq.num_free==16U && port.linux_vq.num_max==16U);

    sg_init_table(output,2);
    sg_init_table(input,1);
    CHECK(nas_linux_virtgpu_sg_map(&output[0],&cmd,16,48)==0);
    CHECK(nas_linux_virtgpu_sg_map(&output[1],&cmd,128,32)==0);
    CHECK(nas_linux_virtgpu_sg_map(&input[0],&response,4,24)==0);
    CHECK(virtqueue_add_sgs(&port.linux_vq,sgs,1,1,&token,0)==0);
    CHECK(q.free_count==13U && port.linux_vq.num_free==13U);
    uint16_t head=q.ring.avail->ring[0], d2=q.ring.desc[head].next;
    uint16_t d3=q.ring.desc[d2].next;
    CHECK(q.ring.desc[head].addr==0x50010U);
    CHECK(q.ring.desc[d2].addr==0x50080U);
    CHECK(q.ring.desc[d3].addr==0x60004U);
    CHECK(!(q.ring.desc[head].flags&VRING_DESC_F_WRITE));
    CHECK(q.ring.desc[d3].flags&VRING_DESC_F_WRITE);
    CHECK(notices==0U);
    CHECK(virtqueue_kick_prepare(&port.linux_vq));
    CHECK(virtqueue_notify(&port.linux_vq));
    CHECK(notices==1U && !virtqueue_kick_prepare(&port.linux_vq));

    virtqueue_disable_cb(&port.linux_vq);
    CHECK(q.ring.avail->flags&VRING_AVAIL_F_NO_INTERRUPT);
    q.ring.used->ring[0].id=head;
    q.ring.used->ring[0].len=12U;
    q.ring.used->idx=1U;
    CHECK(!virtqueue_enable_cb(&port.linux_vq));
    CHECK(virtqueue_get_buf(&port.linux_vq,&len)==&token && len==12U);
    CHECK(virtqueue_enable_cb(&port.linux_vq));
    CHECK(port.linux_vq.num_free==16U && q.inflight==0U);
    CHECK(virtqueue_get_buf(&port.linux_vq,&len)==NULL);

    /* An unpinned or stale DMA SG can never reach device-visible memory. */
    sg_init_table(output,1);
    CHECK(virtqueue_add_sgs(&port.linux_vq,sgs,1,0,&token,0)==-EINVAL);
    CHECK(q.ring.avail->idx==1U);
    CHECK(nas_linux_virtgpu_sg_map(&output[0],&cmd,8,16)==0);
    cmd.usable=false;
    CHECK(virtqueue_add_sgs(&port.linux_vq,sgs,1,0,&token,0)==-EINVAL);
    cmd.usable=true;
    output[0].dma_address++;
    CHECK(virtqueue_add_sgs(&port.linux_vq,sgs,1,0,&token,0)==-EINVAL);
    output[0].dma_address--;
    CHECK(virtqueue_add_sgs(&port.linux_vq,sgs,1,0,NULL,0)==-EINVAL);

    /* Direct split ring only. A submission requiring more descriptors
     * than free_count must fail, never sleep forever waiting for space. */
    struct scatterlist long_sgs[17];
    struct scatterlist *long_list[]={long_sgs};
    sg_init_table(long_sgs,17);
    for(unsigned i=0;i<17;i++)
        CHECK(nas_linux_virtgpu_sg_map(&long_sgs[i],&cmd,0,16)==0);
    CHECK(virtqueue_add_sgs(&port.linux_vq,long_list,1,0,&token,0)==-ENOSPC);
    CHECK(q.free_count==16U && port.linux_vq.num_free==16U);

    /* The real SG_CHAIN marker must traverse to the second allocation. */
    struct scatterlist chain[2],second[1];
    sg_init_table(chain,2);
    sg_init_table(second,1);
    CHECK(nas_linux_virtgpu_sg_map(&chain[0],&cmd,0,8)==0);
    CHECK(nas_linux_virtgpu_sg_map(&second[0],&cmd,16,8)==0);
    chain[1].page_link=((uintptr_t)second)|SG_CHAIN;
    struct scatterlist *linked[]={chain};
    CHECK(virtqueue_add_sgs(&port.linux_vq,linked,1,0,&token,0)==0);
    CHECK(port.linux_vq.num_free==14U && !q.poisoned);
    uint16_t head2=q.ring.avail->ring[1], tail=q.ring.desc[head2].next;
    CHECK(q.ring.desc[head2].addr==0x50000U);
    CHECK(q.ring.desc[tail].addr==0x50010U);
    CHECK(virtqueue_kick_prepare(&port.linux_vq));
    CHECK(virtqueue_notify(&port.linux_vq));
    q.ring.used->ring[1].id=head2;
    q.ring.used->ring[1].len=0;
    q.ring.used->idx=2;
    CHECK(virtqueue_get_buf(&port.linux_vq,&len)==&token);
    CHECK(port.linux_vq.num_free==16U);

    CHECK(nas_vring_queue_close(&q)==0);
    CHECK(notices==2U && resets==1U && releases==1U && failed==0U);
    return 0;
}
int main(void) {return test();}
