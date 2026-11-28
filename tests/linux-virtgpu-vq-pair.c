/* Integration test of REAL NaSeed virtio transport, Unikraft split-ring,
 * Linux-named virtqueue_* adapter and GPU control+cursor queue lifecycle.
 * Hardware registers, IRQ source, and coherent page allocation are mocked.
 * Does not emulate GPU 3D responses, DRM ioctls or Mesa acceleration. */
#include "../ports/linux-virtgpu/vq_pair_port.h"
#include "../kernel/errno.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define CHECK(x) do {if(!(x))return __LINE__;} while(0)
static unsigned char ring_pages[2][NAS_DMA_PAGE_BYTES]
    __attribute__((aligned(4096)));
static unsigned alloc_count,released,notifies,resets;
static unsigned callbacks_control,callbacks_cursor;
static uint32_t status;
static bool fail_cursor_queue;
static uint64_t feature_mask=NAS_VIRTIO_F_VERSION_1|1U;
static void hw_reset(void *ctx) {(void)ctx;status=0U;resets++;}
static uint32_t hw_status(void *ctx) {(void)ctx;return status;}
static void hw_set_status(void *ctx,uint32_t s) {(void)ctx;status=s;}
static uint32_t hw_read_features(void *ctx,unsigned bank) {
    (void)ctx;
    return bank==0U ? (uint32_t)feature_mask:
           bank==1U ? (uint32_t)(feature_mask>>32):0U;
}
static void hw_write_features(void *ctx,unsigned bank,uint32_t bits) {
    (void)ctx;(void)bank;(void)bits;
}
static int hw_setup_queue(void *ctx,unsigned id,unsigned n,
                          uint64_t desc,uint64_t avail,uint64_t used) {
    (void)ctx;
    if(fail_cursor_queue && id==1U)return -EIO;
    if(id>1U || n!=16U || !desc || !avail || !used)return -EINVAL;
    return 0;
}
static void hw_notify(void *ctx,unsigned id) {
    (void)ctx;(void)id;notifies++;
}
static uint32_t hw_read_config32(void *ctx,uint32_t offset) {
    (void)ctx;(void)offset;return 1U;
}
static unsigned char provider_dummy;
static int dma_allocate(void *ctx,struct nas_dma_page *p) {
    (void)ctx;
    if(alloc_count>=2U)return -ENOMEM;
    unsigned n=alloc_count++;
    *p=(struct nas_dma_page){
        .bus_address=0x100000U+n*0x10000U,.frame_pa=0x3000U+n*0x1000U,
        .cpu=ring_pages[n],.bytes=NAS_DMA_PAGE_BYTES};
    return 0;
}
static int dma_free(void *ctx,struct nas_dma_page *p) {
    (void)ctx;
    if(!p || !p->cpu)return -EINVAL;
    released++;
    *p=(struct nas_dma_page){0};
    return 0;
}
int nas_dma_alloc_page(const struct nas_dma_provider *d,struct nas_dma_page *p) {
    return d && d->alloc_page ? d->alloc_page(d->context,p) : -EINVAL;
}
int nas_dma_free_page(const struct nas_dma_provider *d,struct nas_dma_page *p) {
    return d && d->release_page ? d->release_page(d->context,p) : -EINVAL;
}
int nas_dma_sync(const struct nas_dma_provider *d,struct nas_dma_page *p,
                 uint32_t offset,uint32_t bytes,enum nas_dma_direction dir) {
    if(!d || !p || !p->cpu || !d->coherent || offset>p->bytes ||
       bytes>p->bytes-offset ||
       (dir!=NAS_DMA_TO_DEVICE && dir!=NAS_DMA_FROM_DEVICE))return -EINVAL;
    return 0;
}
static void on_control(struct virtqueue *vq) {
    if(vq && vq->index==0U)callbacks_control++;
}
static void on_cursor(struct virtqueue *vq) {
    if(vq && vq->index==1U)callbacks_cursor++;
}
int main(void) {
    const struct nas_dma_provider dma={
        .context=&provider_dummy,.coherent=true,
        .alloc_page=dma_allocate,.release_page=dma_free};
    const struct nas_virtio_transport_ops ops={
        .reset=hw_reset,.read_status=hw_status,
        .write_status=hw_set_status,.read_features=hw_read_features,
        .write_features=hw_write_features,.setup_queue=hw_setup_queue,
        .notify_queue=hw_notify,.read_config32=hw_read_config32};
    const struct nas_virtio_transport transport={
        .context=&status,.ops=&ops,.dma=&dma};
    struct nas_linux_virtgpu_vq_pair pair={0};
    uint64_t features=0U;
    CHECK(nas_virtio_negotiate(&transport,0U,1U,&features)==0);
    CHECK(features==(NAS_VIRTIO_F_VERSION_1|1U));
    CHECK(nas_linux_virtgpu_vq_pair_init(&pair,&transport,NULL,16U,
                                        on_control,on_cursor)==0);
    CHECK(pair.initialized && pair.rings[0].queue_id==0U &&
          pair.rings[1].queue_id==1U);
    CHECK(pair.vqs[0].linux_vq.callback==on_control);
    CHECK(pair.vqs[1].linux_vq.callback==on_cursor);
    CHECK(pair.vqs[0].linux_vq.num_free==16U &&
          pair.vqs[1].linux_vq.num_free==16U);
    CHECK(alloc_count==2U);
    CHECK(nas_linux_virtgpu_vq_pair_activate(&pair)==0);
    CHECK(status & NAS_VIRTIO_STATUS_DRIVER_OK);
    CHECK(nas_linux_virtgpu_vq_pair_service(&pair)==0);
    nas_linux_virtgpu_vq_pair_irq_record(&pair,NAS_VIRTIO_IRQ_CONFIG_CHANGE);
    CHECK(nas_linux_virtgpu_vq_pair_service(&pair)==0);
    CHECK(callbacks_control==0U && callbacks_cursor==0U);

    struct nas_dma_mapped_frame mapped={
        .provider=&dma,.frame_pa=0x9000U,.bus_address=0x700000U,
        .active=true,.usable=true};
    struct scatterlist cmd;
    sg_init_table(&cmd,1U);
    int ctrl_cookie=7,cursor_cookie=11;
    CHECK(nas_linux_virtgpu_sg_map(&pair.vqs[0],&cmd,&mapped,0U,16U)==0);
    struct scatterlist *sgs[1]={&cmd};
    CHECK(virtqueue_add_sgs(&pair.vqs[0].linux_vq,sgs,1U,0U,
                            &ctrl_cookie,0U)==0);
    uint16_t ctrl_head=pair.rings[0].ring.avail->ring[0];
    CHECK(virtqueue_kick_prepare(&pair.vqs[0].linux_vq));
    CHECK(virtqueue_notify(&pair.vqs[0].linux_vq));
    pair.rings[0].ring.used->ring[0].id=ctrl_head;
    pair.rings[0].ring.used->ring[0].len=9U;
    pair.rings[0].ring.used->idx=1U;
    nas_linux_virtgpu_vq_pair_irq_record(&pair,NAS_VIRTIO_IRQ_USED_BUFFER);
    CHECK(callbacks_control==0U); /* Hard IRQ must never call Linux work. */
    CHECK(nas_linux_virtgpu_vq_pair_service(&pair)==1);
    CHECK(callbacks_control==1U && callbacks_cursor==0U);
    unsigned int len=0U;
    CHECK(virtqueue_get_buf(&pair.vqs[0].linux_vq,&len)==&ctrl_cookie &&
          len==9U && pair.vqs[0].linux_vq.num_free==16U);

    sg_init_table(&cmd,1U);
    CHECK(nas_linux_virtgpu_sg_map(&pair.vqs[1],&cmd,&mapped,64U,16U)==0);
    CHECK(virtqueue_add_sgs(&pair.vqs[1].linux_vq,sgs,1U,0U,
                            &cursor_cookie,0U)==0);
    uint16_t cursor_head=pair.rings[1].ring.avail->ring[0];
    CHECK(virtqueue_kick_prepare(&pair.vqs[1].linux_vq));
    CHECK(virtqueue_notify(&pair.vqs[1].linux_vq));
    pair.rings[1].ring.used->ring[0].id=cursor_head;
    pair.rings[1].ring.used->ring[0].len=0U;
    pair.rings[1].ring.used->idx=1U;
    nas_linux_virtgpu_vq_pair_irq_record(&pair,NAS_VIRTIO_IRQ_USED_BUFFER);
    CHECK(nas_linux_virtgpu_vq_pair_service(&pair)==1);
    CHECK(callbacks_control==1U && callbacks_cursor==1U);
    CHECK(virtqueue_get_buf(&pair.vqs[1].linux_vq,&len)==&cursor_cookie);

    /* Abandoned GPU command cookies remain device-owned until actual reset.
     * Do not invent completion or signal a fence during forced teardown. */
    sg_init_table(&cmd,1U);
    CHECK(nas_linux_virtgpu_sg_map(&pair.vqs[0],&cmd,&mapped,0U,16U)==0);
    CHECK(virtqueue_add_sgs(&pair.vqs[0].linux_vq,sgs,1U,0U,
                            &ctrl_cookie,0U)==0);
    CHECK(nas_linux_virtgpu_vq_pair_close(&pair)==0);
    CHECK(released==2U && !(status & NAS_VIRTIO_STATUS_DRIVER_OK));
    unsigned cursor=0U;void *abandoned=NULL;
    CHECK(nas_linux_virtgpu_vq_reap_after_reset(&pair.vqs[0].bridge,
                                                &cursor,&abandoned)==0);
    CHECK(abandoned==&ctrl_cookie);
    CHECK(nas_linux_virtgpu_vq_reap_after_reset(&pair.vqs[0].bridge,
                                                &cursor,&abandoned)==-ENOENT);
    CHECK(callbacks_control==1U && callbacks_cursor==1U);
    CHECK(notifies==2U);
    return 0;
}
