/* Deterministic owner-preservation oracle for the real M178 async class.
 * Failure injection covers the IRQ, vring, each DMA slot and failed probe.
 * Real hardware IRQ and persisted filesystem data remain M178/M179/M180's
 * separate mandatory three-ISA QEMU evidence. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ports/uboot/async_blk.c"
#include "../kernel/block/request.c"

static const struct nas_bus_driver *registered_driver;
static struct nas_irq_action *active_irq;
static unsigned allocated,freed,resets,free_attempts;
static bool fail_irq_unbind,fail_ring_close,fail_activate,fail_queue_init;
static unsigned fail_free_attempt;
static uint8_t device_cookie;
static const struct nas_dma_provider dma={.coherent=true};

void nas_print(const char *s) {(void)s;}
void nas_dec(uint64_t n) {(void)n;}
void nas_arch_halt(void) {abort();}
int nas_bus_driver_register(const struct nas_bus_driver *d) {
    assert(d && !registered_driver);registered_driver=d;return 0;
}
int nas_dma_alloc_page(const struct nas_dma_provider *provider,
                       struct nas_dma_page *page) {
    assert(provider==&dma && page && !page->cpu);
    void *memory=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    assert(memory);memset(memory,0,NAS_DMA_PAGE_BYTES);
    *page=(struct nas_dma_page){.cpu=memory,
        .bus_address=(uint64_t)(uintptr_t)memory,.bytes=NAS_DMA_PAGE_BYTES};
    ++allocated;return 0;
}
int nas_dma_free_page(const struct nas_dma_provider *provider,
                      struct nas_dma_page *page) {
    assert(provider==&dma && page && page->cpu);
    if(++free_attempts==fail_free_attempt) {
        fail_free_attempt=0;return -EIO;
    }
    free(page->cpu);memset(page,0,sizeof(*page));++freed;return 0;
}
int nas_dma_sync(const struct nas_dma_provider *provider,
                 struct nas_dma_page *page,uint32_t offset,uint32_t bytes,
                 enum nas_dma_direction direction) {
    assert(provider==&dma && page && page->cpu);
    assert(offset<=NAS_DMA_PAGE_BYTES && bytes<=NAS_DMA_PAGE_BYTES-offset);
    assert(direction==NAS_DMA_TO_DEVICE || direction==NAS_DMA_FROM_DEVICE);
    return 0;
}
int nas_virtio_config_read(const struct nas_virtio_transport *transport,
                           uint32_t offset,void *destination,uint32_t bytes) {
    assert(transport && !offset && bytes==8U);
    uint64_t capacity=NAS_ASYNC_BLK_FIXTURE_SECTORS;
    memcpy(destination,&capacity,sizeof(capacity));return 0;
}
uint32_t nas_virtio_config32(const struct nas_virtio_transport *transport,
                             uint32_t offset) {
    assert(transport && offset==20U);return 512U;
}
int nas_virtio_negotiate(const struct nas_virtio_transport *transport,
                         uint64_t required,uint64_t optional,uint64_t *accepted) {
    assert(transport && !required && (optional&BLK_F_FLUSH));
    *accepted=BLK_F_BLK_SIZE|BLK_F_FLUSH;return 0;
}
int nas_virtio_activate(const struct nas_virtio_transport *transport) {
    assert(transport);
    if(fail_activate){fail_activate=false;return -EIO;}
    return 0;
}
uint32_t nas_virtio_irq_ack(const struct nas_virtio_transport *transport) {
    assert(transport);return 0;
}
uint32_t nas_virtio_irq_ack_source(void *context) {
    assert(context);return NAS_VIRTIO_IRQ_USED_BUFFER;
}
int nas_irq_bind(struct nas_irq_action *action,struct nas_irq_route route,
                 nas_irq_source_ack_fn source,void *source_context,
                 nas_irq_notify_fn notify,void *notify_context) {
    assert(action && !active_irq && source && source_context && notify && notify_context);
    *action=(struct nas_irq_action){.route=route,.source_ack=source,
        .source_context=source_context,.notify=notify,
        .notify_context=notify_context,.bound=true};
    active_irq=action;return 0;
}
int nas_irq_unbind(struct nas_irq_action *action) {
    assert(action);
    if(!action->bound)return 0;
    assert(active_irq==action);
    if(fail_irq_unbind){fail_irq_unbind=false;return -EIO;}
    active_irq=NULL;action->bound=false;return 0;
}
uint64_t nas_arch_monotonic_ns(void) {return 1000;}
static void reset_transport(void *context) {
    assert(context==&device_cookie);++resets;
}
static uint32_t ack_transport(void *context) {
    assert(context==&device_cookie);return 1;
}
int nas_vring_queue_init(struct nas_vring_queue *queue,
                         const struct nas_virtio_transport *transport,
                         unsigned id,unsigned desc) {
    assert(queue && transport && !id && desc==BLK_RING_DESC);
    queue->transport=transport;
    if(fail_queue_init) {
        fail_queue_init=false;
        assert(!nas_dma_alloc_page(transport->dma,&queue->dma_page));
        /* Inactive but still owned: the common queue can fail its own
         * initialization rollback with a provider rejection. */
        return -EIO;
    }
    queue->active=true;return 0;
}
int nas_vring_queue_close(struct nas_vring_queue *queue) {
    assert(queue && (queue->active || queue->dma_page.cpu));
    if(fail_ring_close){fail_ring_close=false;return -EIO;}
    reset_transport(queue->transport->context);
    if(queue->dma_page.cpu) {
        int rc=nas_dma_free_page(queue->transport->dma,&queue->dma_page);
        if(rc)return rc;
    }
    memset(queue,0,sizeof(*queue));return 0;
}
int nas_vring_queue_submit(struct nas_vring_queue *queue,
                           const struct nas_vring_segment *segs,
                           unsigned count,uint16_t *head) {
    (void)queue;(void)segs;(void)count;(void)head;
    return -EOPNOTSUPP;
}
int nas_vring_queue_poll(struct nas_vring_queue *queue,
                         uint16_t *head,uint32_t *bytes) {
    (void)queue;(void)head;(void)bytes;return -EAGAIN;
}

static const struct nas_virtio_transport transport={
    .context=&device_cookie,
    .ops=&(const struct nas_virtio_transport_ops){
        .reset=reset_transport,.read_ack_irq=ack_transport
    },
    .dma=&dma
};
static struct nas_bus_device dev={
    .bus=NAS_BUS_VIRTIO_MMIO,.device_id=VIRTIO_ID_BLOCK,
    .resource={.irq={.domain=NAS_IRQ_DOMAIN_RISCV_PLIC,.hwirq=9U}},
    .virtio=&transport
};

static void clean_case(void) {
    assert(!disk.owner && !disk.ready && !disk.teardown_pending);
    assert(!active_irq && allocated==freed);
    allocated=freed=resets=free_attempts=0;
    fail_free_attempt=0;
    fail_irq_unbind=fail_ring_close=fail_activate=fail_queue_init=false;
}
static void attach_case(void) {
    assert(registered_driver->probe(&dev)==0);
    assert(disk.owner==&dev && disk.ready && !disk.teardown_pending);
    assert(nas_async_blk_owns_device(&dev));
    assert(active_irq && allocated==16U);
}
static void complete_detach_case(void) {
    assert(registered_driver->remove(&dev)==0);
    clean_case();
}
int main(void) {
    assert(nas_async_blk_driver_register()==0 && registered_driver);
    attach_case();
    fail_irq_unbind=true;
    assert(registered_driver->remove(&dev)==-EIO);
    assert(disk.owner==&dev && disk.teardown_pending && !disk.ready);
    assert(active_irq && nas_async_blk_owns_device(&dev));
    assert(allocated==16U && freed==0);
    complete_detach_case();

    attach_case();
    fail_ring_close=true;
    assert(registered_driver->remove(&dev)==-EIO);
    assert(disk.ring.active && disk.owner==&dev && disk.teardown_pending);
    assert(!active_irq && freed==0);
    complete_detach_case();

    attach_case();
    fail_free_attempt=4U;
    assert(registered_driver->remove(&dev)==-EIO);
    assert(freed==3U && disk.slots[1].payload.cpu);
    assert(disk.owner==&dev && disk.teardown_pending);
    assert(!disk.ready && nas_async_blk_owns_device(&dev));
    complete_detach_case();

    fail_activate=true;
    fail_free_attempt=1U;
    assert(registered_driver->probe(&dev)==-EIO);
    assert(disk.owner==&dev && disk.teardown_pending && !disk.ready);
    assert(disk.slots[0].header.cpu && allocated==16U && !freed);
    assert(registered_driver->probe(&dev)==0);
    assert(allocated==32U && freed==16U && active_irq);
    complete_detach_case();

    fail_queue_init=true;
    fail_free_attempt=1U;
    assert(registered_driver->probe(&dev)==-EIO);
    assert(disk.owner==&dev && disk.teardown_pending &&
           !disk.ring.active && disk.ring.dma_page.cpu);
    assert(allocated==17U && !freed);
    assert(registered_driver->probe(&dev)==0);
    assert(allocated==33U && freed==17U && active_irq);
    complete_detach_case();
    puts("[M184 ASYNC BLK LIFETIME PASS] IRQ/vring/DMA release failures, failed-probe orphan, inactive owned queue and exact-once retry");
    return 0;
}
