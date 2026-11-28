/* Exercise the REAL NaSeed Unikraft split queue and port cookie bridge.
 * Mock ONLY the hardware transport and coherent DMA page provider.
 * This is not a VirGL renderer, and cannot substitute for QEMU acceptance.
 */
#include "../ports/linux-virtgpu/virtqueue_bridge.h"
#include "../kernel/errno.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static unsigned char dma_memory[NAS_DMA_PAGE_BYTES] __attribute__((aligned(4096)));
static unsigned notifications, resets, failures, dma_releases;
static bool fail_next_sync;

int nas_dma_alloc_page(const struct nas_dma_provider *p,struct nas_dma_page *page) {
    (void)p;
    *page=(struct nas_dma_page){.bus_address=0x100000U,.cpu=dma_memory,
                                .bytes=NAS_DMA_PAGE_BYTES};
    return 0;
}
int nas_dma_free_page(const struct nas_dma_provider *p,struct nas_dma_page *page) {
    (void)p;
    ++dma_releases;
    *page=(struct nas_dma_page){0};
    return 0;
}
int nas_dma_sync(const struct nas_dma_provider *p,struct nas_dma_page *page,
                 uint32_t offset,uint32_t bytes,enum nas_dma_direction direction) {
    (void)p;(void)direction;
    if (fail_next_sync) { fail_next_sync=false; return -EIO; }
    return page && page->cpu && offset<=page->bytes &&
           bytes<=page->bytes-offset ? 0:-EIO;
}
int nas_virtio_setup_queue(const struct nas_virtio_transport *t,unsigned id,
                           unsigned count,uint64_t desc,uint64_t driver,uint64_t device) {
    (void)t;(void)id;
    return count==16U && desc && driver && device ? 0:-EIO;
}
void nas_virtio_notify(const struct nas_virtio_transport *t,unsigned id) {
    (void)t;(void)id;++notifications;
}
void nas_virtio_fail(const struct nas_virtio_transport *t) {
    (void)t;++failures;
}
static void reset(void *ctx) {(void)ctx;++resets;}

static int check_bridge(void) {
    const struct nas_dma_provider dma={.coherent=true};
    const struct nas_virtio_transport_ops ops={.reset=reset};
    const struct nas_virtio_transport transport={
        .context=&notifications,.ops=&ops,.dma=&dma};
    struct nas_vring_queue q={0};
    struct nas_linux_virtgpu_vq_bridge bridge={0};
    struct nas_vring_segment segs[2]={
        {.bus_address=0x210000U,.length=40U,.device_writes=false},
        {.bus_address=0x220000U,.length=24U,.device_writes=true}
    };
    int token1=1,token2=2,token3=3;
    void *returned=NULL;
    uint32_t used=0;
    uint16_t head1,head2,head3;

    CHECK(nas_vring_queue_init(&q,&transport,0,16)==0);
    CHECK(nas_linux_virtgpu_vq_bind(&bridge,&q)==0);
    /* The queue consumes provider-mapped slices (not virtual pointers).
     * The DMA provider's actual frame pin/unpin is exercised separately
     * by tests/linux-virtgpu-dma-frames.c against the REAL dma.c. */
    struct nas_dma_mapped_frame mapped_cmd={
        .provider=&dma,.frame_pa=0x1000,.bus_address=0x210000,.active=true};
    struct nas_dma_mapped_frame mapped_resp={
        .provider=&dma,.frame_pa=0x2000,.bus_address=0x220000,.active=true};
    struct nas_linux_virtgpu_dma_sg sg[2]={
        {.map=&mapped_cmd,.offset=32,.length=40,.device_writes=false},
        {.map=&mapped_resp,.offset=128,.length=24,.device_writes=true}
    };
    CHECK(nas_linux_virtgpu_vq_submit_mapped(&bridge,sg,2,&token1)==0);
    head1=q.ring.avail->ring[0];
    CHECK(q.ring.desc[head1].addr==0x210020U);
    CHECK(q.ring.desc[head1].len==40U);
    CHECK(q.ring.desc[q.ring.desc[head1].next].addr==0x220080U);
    CHECK(q.ring.desc[q.ring.desc[head1].next].flags==VRING_DESC_F_WRITE);
    CHECK(nas_linux_virtgpu_vq_submit(&bridge,segs,2,&token2)==0);
    head2=q.ring.avail->ring[1];
    CHECK(head1!=head2 && q.free_count==12U);
    CHECK(notifications==0U && q.kick_pending);
    CHECK(nas_linux_virtgpu_vq_reclaim(&bridge,&returned,&used)==-EAGAIN);
    CHECK(nas_linux_virtgpu_vq_kick_prepare(&bridge));
    CHECK(!nas_linux_virtgpu_vq_kick_prepare(&bridge));
    nas_linux_virtgpu_vq_notify(&bridge);
    CHECK(notifications==1U);

    /* Completions can arrive in a different order than submissions. */
    q.ring.used->ring[0].id=head2;
    q.ring.used->ring[0].len=17U;
    q.ring.used->idx=1U;
    CHECK(nas_linux_virtgpu_vq_reclaim(&bridge,&returned,&used)==0);
    CHECK(returned==&token2 && used==17U);
    q.ring.used->ring[1].id=head1;
    q.ring.used->ring[1].len=23U;
    q.ring.used->idx=2U;
    CHECK(nas_linux_virtgpu_vq_reclaim(&bridge,&returned,&used)==0);
    CHECK(returned==&token1 && used==23U && q.free_count==16U);

    nas_linux_virtgpu_vq_disable_callbacks(&bridge);
    CHECK(q.ring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT);
    CHECK(nas_linux_virtgpu_vq_enable_callbacks(&bridge));
    CHECK(!(q.ring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT));

    /* NO_NOTIFY is a real VirtIO device hint, not a failed submission. */
    q.ring.used->flags=VRING_USED_F_NO_NOTIFY;
    CHECK(nas_linux_virtgpu_vq_submit(&bridge,segs,2,&token3)==0);
    head3=q.ring.avail->ring[2];
    CHECK(!nas_linux_virtgpu_vq_kick_prepare(&bridge));
    CHECK(notifications==1U);
    q.ring.used->ring[2].id=head3;
    q.ring.used->ring[2].len=19U;
    q.ring.used->idx=3U;
    /* A completion arrived while callbacks were suppressed: enable must
     * return false and force the Linux driver to drain before sleeping. */
    nas_linux_virtgpu_vq_disable_callbacks(&bridge);
    CHECK(!nas_linux_virtgpu_vq_enable_callbacks(&bridge));
    CHECK(nas_linux_virtgpu_vq_reclaim(&bridge,&returned,&used)==0);
    CHECK(nas_linux_virtgpu_vq_enable_callbacks(&bridge));
    CHECK(returned==&token3 && used==19U && q.free_count==16U);

    /* Existing block/input/GPU eager-submit semantics remain unchanged. */
    q.ring.used->flags=0U;
    CHECK(nas_vring_queue_submit(&q,segs,2,&head3)==0);
    CHECK(notifications==2U && !q.kick_pending);
    q.ring.used->ring[3].id=head3;
    q.ring.used->ring[3].len=7U;
    q.ring.used->idx=4U;
    uint16_t completed=0;
    CHECK(nas_vring_queue_poll(&q,&completed,&used)==0);
    CHECK(completed==head3 && used==7U && q.free_count==16U);
    CHECK(nas_vring_queue_close(&q)==0);
    CHECK(resets==1U && dma_releases==1U && failures==0U);

    /* A postpublish DMA-sync failure is NOT a cancelled request.
     * Cookie must survive until the queue has explicitly reset the device. */
    CHECK(nas_vring_queue_init(&q,&transport,0,16)==0);
    CHECK(nas_linux_virtgpu_vq_bind(&bridge,&q)==0);
    unsigned cursor=0U;
    CHECK(nas_linux_virtgpu_vq_reap_after_reset(&bridge,&cursor,&returned)
          ==-EBUSY);
    uint16_t abandoned_head=q.free_head;
    fail_next_sync=true;
    CHECK(nas_linux_virtgpu_vq_submit(&bridge,segs,2,&token1)==-EIO);
    CHECK(q.poisoned && q.pending[abandoned_head]);
    CHECK(bridge.cookies[abandoned_head]==&token1);
    CHECK(nas_linux_virtgpu_vq_reap_after_reset(&bridge,&cursor,&returned)
          ==-EBUSY);
    CHECK(nas_vring_queue_close(&q)==0);
    cursor=0U;
    CHECK(nas_linux_virtgpu_vq_reap_after_reset(&bridge,&cursor,&returned)==0);
    CHECK(returned==&token1);
    CHECK(nas_linux_virtgpu_vq_reap_after_reset(&bridge,&cursor,&returned)
          ==-ENOENT);
    CHECK(resets==2U && dma_releases==2U && failures==1U);
    return 0;
}
int main(void) { return check_bridge(); }
