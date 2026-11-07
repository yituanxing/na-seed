/* Exercises NaSeed's adapter using the REAL pinned Unikraft vring layout.
 * Synthetic device completions do not establish real VirtIO-Blk I/O. */
#include "../ports/unikraft/split_queue.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static _Alignas(4096) uint8_t pages[2][NAS_DMA_PAGE_BYTES];
static bool used[2];
struct faulty_dma {
    unsigned releases_failed;
    unsigned release_attempts;
    unsigned release_successes;
    bool unaligned_bus;
    bool invalid_page_size;
};
static int faulty_alloc(void *context, struct nas_dma_page *page) {
    struct faulty_dma *fault=context;
    int rc=nas_dma_qemu_coherent.alloc_page(NULL,page);
    if (!rc && fault->unaligned_bus) page->bus_address++;
    if (!rc && fault->invalid_page_size) page->bytes--;
    return rc;
}
static int faulty_free(void *context, struct nas_dma_page *page) {
    struct faulty_dma *fault=context;
    fault->release_attempts++;
    if (fault->releases_failed) {
        fault->releases_failed--;
        return -EIO;
    }
    int rc=nas_dma_qemu_coherent.release_page(NULL,page);
    if (!rc) fault->release_successes++;
    return rc;
}
uint64_t nas_hhdm_offset(void) { return 0; }
uint64_t nas_frame_alloc(void) {
    for (unsigned i=0;i<2;i++) if (!used[i]) {
        used[i]=true;
        return (uint64_t)(uintptr_t)pages[i];
    }
    return 0;
}
bool nas_frame_put(uint64_t pa) {
    for (unsigned i=0;i<2;i++) if (used[i] && pa==(uintptr_t)pages[i]) {
        used[i]=false; return true;
    }
    return false;
}
struct mock { uint32_t status; uint64_t supported; uint64_t accepted;
              unsigned resets, setups, notifies; uint64_t desc, avail, used; };
static void reset(void *v) {struct mock *m=v;m->status=0;m->resets++;}
static uint32_t rd_status(void *v) {return ((struct mock*)v)->status;}
static void wr_status(void *v,uint32_t x) {((struct mock*)v)->status=x;}
static uint32_t rd_features(void *v,unsigned bank) {
    return (uint32_t)(((struct mock*)v)->supported>>(bank*32));
}
static void wr_features(void *v,unsigned bank,uint32_t x) {
    struct mock *m=v;
    if (bank==0) m->accepted=(m->accepted & UINT64_C(0xffffffff00000000))|x;
    else m->accepted=(m->accepted&UINT64_C(0xffffffff))|((uint64_t)x<<32);
}
static int setup(void *v,unsigned id,unsigned n,uint64_t d,uint64_t a,uint64_t u) {
    struct mock *m=v;
    if (id!=0||n!=8) return -EINVAL;
    m->setups++;m->desc=d;m->avail=a;m->used=u;return 0;
}
static void notify(void *v,unsigned id) {
    assert(id==0);((struct mock*)v)->notifies++;
}
static uint32_t read_config(void *v,uint32_t off){(void)v;(void)off;return 0;}
static const struct nas_virtio_transport_ops ops={
  .reset=reset,.read_status=rd_status,.write_status=wr_status,
  .read_features=rd_features,.write_features=wr_features,
  .setup_queue=setup,.notify_queue=notify,.read_config32=read_config
};
static void test_retriable_dma_ownership(void) {
    struct mock dev={.supported=NAS_VIRTIO_F_VERSION_1};
    struct faulty_dma fault={.releases_failed=2};
    const struct nas_dma_provider dma={.context=&fault,.coherent=true,
        .alloc_page=faulty_alloc,.release_page=faulty_free};
    const struct nas_virtio_transport tr={.context=&dev,.ops=&ops,.dma=&dma};
    struct nas_vring_queue q={0};

    /* queue 1 is deliberately rejected by the mock transport *after* a
     * ring page is owned. The failed rollback must retain that owner. */
    assert(nas_vring_queue_init(&q,&tr,1,8)==-EINVAL);
    assert(!q.active && q.dma_page.cpu && q.transport==&tr);
    assert(q.poisoned && dev.resets==1 && fault.release_attempts==1);
    assert(used[0] && !used[1]);
    assert(nas_vring_queue_init(&q,&tr,0,8)==-EBUSY);
    assert(nas_vring_queue_close(&q)==-EIO);
    assert(!q.active && q.dma_page.cpu && fault.release_attempts==2);
    assert(used[0] && dev.resets==1);
    assert(nas_vring_queue_close(&q)==0);
    assert(!q.dma_page.cpu && !used[0] && !used[1]);
    assert(fault.release_attempts==3 && fault.release_successes==1);

    /* After the orphan is drained, a later probe can reuse the queue. */
    assert(nas_vring_queue_init(&q,&tr,0,8)==0 && q.active);
    fault.releases_failed=1;
    assert(nas_vring_queue_close(&q)==-EIO);
    assert(!q.active && q.dma_page.cpu && q.poisoned);
    assert(dev.resets==2 && used[0]);
    uint16_t head=0;
    const struct nas_vring_segment seg={.length=16};
    assert(nas_vring_queue_submit(&q,&seg,1,&head)==-EINVAL);
    assert(nas_vring_queue_init(&q,&tr,0,8)==-EBUSY);
    assert(nas_vring_queue_close(&q)==0);
    assert(dev.resets==2 && !used[0] && !used[1]);
    assert(fault.release_successes==2);

    /* Before setup_queue, an invalid DMA alignment has no device owner;
     * even then a failed release must not erase the page's CPU pointer. */
    fault.unaligned_bus=true;
    fault.releases_failed=1;
    assert(nas_vring_queue_init(&q,&tr,0,8)==-EINVAL);
    assert(!q.active && q.dma_page.cpu && used[0]);
    assert(dev.resets==2);
    assert(nas_vring_queue_close(&q)==0);
    assert(!used[0] && !used[1] && fault.release_successes==3);

    /* A DMA provider can return an invalid mapped size AND fail to free
     * it when nas_dma_alloc_page attempts its own rollback. This never
     * reaches queue setup, yet its page is still owned by the queue. */
    fault.unaligned_bus=false;
    fault.invalid_page_size=true;
    fault.releases_failed=2;
    assert(nas_vring_queue_init(&q,&tr,0,8)==-EIO);
    assert(!q.active && q.dma_page.cpu && q.transport==&tr);
    assert(q.poisoned && q.dma_page.bytes==NAS_DMA_PAGE_BYTES-1U);
    assert(nas_vring_queue_init(&q,&tr,0,8)==-EBUSY);
    assert(nas_vring_queue_close(&q)==-EIO);
    assert(q.dma_page.cpu && used[0]);
    assert(nas_vring_queue_close(&q)==0);
    assert(!q.dma_page.cpu && !used[0] && !used[1]);
    assert(dev.resets==2 && fault.release_successes==4);
}
int main(void) {
    struct mock dev={.supported=NAS_VIRTIO_F_VERSION_1};
    const struct nas_virtio_transport transport={.context=&dev,.ops=&ops,
        .dma=&nas_dma_qemu_coherent};
    struct nas_vring_queue q={0};
    assert(nas_vring_queue_init(&q,&transport,0,7)==-EINVAL);
    assert(nas_vring_queue_init(&q,&transport,0,8)==0);
    assert(q.free_count==8&&q.ring.num==8);
    assert(dev.desc==(uintptr_t)q.dma_page.cpu);
    assert(dev.avail==dev.desc+((uintptr_t)q.ring.avail-(uintptr_t)q.dma_page.cpu));
    assert(dev.used==dev.desc+((uintptr_t)q.ring.used-(uintptr_t)q.dma_page.cpu));
    uint16_t h1=99,h2=99,done=99;uint32_t len=99;
    struct nas_vring_segment s[3]={{.bus_address=0,.length=16},
           {.bus_address=0x2000,.length=512,.device_writes=true},
           {.bus_address=0x3000,.length=1,.device_writes=true}};
    assert(nas_vring_queue_submit(&q,s,3,&h1)==0);
    assert(h1==0&&q.free_count==5&&q.inflight==1);
    assert(q.ring.desc[h1].addr==0);
    assert(q.ring.desc[1].flags==(VRING_DESC_F_NEXT|VRING_DESC_F_WRITE));
    assert(q.ring.avail->idx==1&&dev.notifies==1);
    assert(nas_vring_queue_poll(&q,&done,&len)==-EAGAIN);
    assert(nas_vring_queue_submit(&q,s,3,&h2)==0);
    assert(h2==3&&q.free_count==2&&q.inflight==2);
    /* The second request may complete first, as in an asynchronous device. */
    q.ring.used->ring[0]=(struct vring_used_elem){.id=h2,.len=513};
    q.ring.used->idx=1;
    assert(nas_vring_queue_poll(&q,&done,&len)==0 && done==h2 && len==513);
    assert(q.inflight==1&&q.free_count==5);
    q.ring.used->ring[1]=(struct vring_used_elem){.id=h1,.len=513};
    q.ring.used->idx=2;
    assert(nas_vring_queue_poll(&q,&done,&len)==0 && done==h1);
    assert(q.inflight==0&&q.free_count==8);
    assert(nas_vring_queue_submit(&q,s,0,&h1)==-EINVAL);
    for (unsigned i = 0; i < 1024; ++i) {
        uint16_t h=UINT16_MAX;
        assert(q.free_count==8 && q.inflight==0);
        assert(nas_vring_queue_submit(&q,s,3,&h)==0);
        uint16_t slot=(uint16_t)(q.ring.used->idx % q.ring.num);
        q.ring.used->ring[slot]=(struct vring_used_elem){.id=h,.len=513};
        q.ring.used->idx++;
        assert(nas_vring_queue_poll(&q,&done,&len)==0 && done==h && len==513);
        assert(q.free_count==8 && q.inflight==0);
    }
    assert(nas_vring_queue_submit(&q,s,3,&h1)==0);
    /* Duplicate or unknown used id is corruption: fail without free-list edits. */
    q.ring.used->ring[q.ring.used->idx % q.ring.num] =
        (struct vring_used_elem){.id=7,.len=0};
    q.ring.used->idx++;
    assert(nas_vring_queue_poll(&q,&done,&len)==-EIO);
    assert(nas_vring_queue_poll(&q,&done,&len)==-EIO);
    assert(nas_vring_queue_submit(&q,s,3,&h1)==-EIO);
    assert(dev.status&NAS_VIRTIO_STATUS_FAILED);
    assert(nas_vring_queue_close(&q)==0);
    assert(!used[0]&&!used[1]&&dev.resets>=1);
    test_retriable_dma_ownership();
    puts("[UPSTREAM VRING ADAPTER PASS] pinned layout, DMA, scatter/gather, 1024 wraps, out-of-order completion, corruption, retryable close/init failure");
    return 0;
}
