/* Compile the real M178 asynchronous VirtIO-BLK class and BlockRequest core
 * verbatim against a deterministic mock PCI/MMIO transport. This Host test
 * proves SG ownership, reverse completion order, status, reset and DMA.
 * Separate native QEMU tests are mandatory for real IRQ and persistent disk. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../ports/uboot/async_blk.c"
#include "../kernel/block/request.c"

enum scenario { GOOD, BAD_USED, BAD_STATUS, TIMEOUT, NO_IRQ, ALLOC_FAIL,
                READ_ONLY, NO_FLUSH, REMOVE_BUSY, BAD_HEAD };
static enum scenario scenario;
static const struct nas_bus_driver *registered_driver;
static struct nas_irq_action *irq;
static unsigned allocs,frees,resets,notifications;
static uint64_t ticks;
static uint8_t storage[32768U*512U];
struct pending_entry {
    struct nas_vring_segment s[3];
    unsigned n;
    uint16_t head;
    bool done,reclaimed;
    uint32_t wrote;
};
static struct pending_entry pending[64];
static unsigned queue_count,completions;

void nas_print(const char *s) {fputs(s,stdout);}
void nas_dec(uint64_t n) {printf("%llu",(unsigned long long)n);}
void nas_arch_halt(void) {abort();}
int nas_bus_driver_register(const struct nas_bus_driver *driver) {
    assert(driver && !registered_driver);
    registered_driver=driver;return 0;
}
int nas_dma_alloc_page(const struct nas_dma_provider *p,struct nas_dma_page *page) {
    assert(p && page && !page->cpu);
    if(scenario==ALLOC_FAIL && allocs==5U)return -ENOMEM;
    void *memory=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    assert(memory);
    memset(memory,0,NAS_DMA_PAGE_BYTES);
    *page=(struct nas_dma_page){.cpu=memory,.bus_address=(uintptr_t)memory,
        .bytes=NAS_DMA_PAGE_BYTES};
    allocs++;return 0;
}
int nas_dma_free_page(const struct nas_dma_provider *p,struct nas_dma_page *page) {
    assert(p && page && page->cpu);
    free(page->cpu);memset(page,0,sizeof(*page));frees++;return 0;
}
int nas_dma_sync(const struct nas_dma_provider *p,struct nas_dma_page *page,
                 uint32_t off,uint32_t bytes,enum nas_dma_direction dir) {
    assert(p && page && page->cpu && off<=NAS_DMA_PAGE_BYTES);
    assert(bytes<=NAS_DMA_PAGE_BYTES-off);
    assert(dir==NAS_DMA_TO_DEVICE || dir==NAS_DMA_FROM_DEVICE);
    return 0;
}
int nas_virtio_config_read(const struct nas_virtio_transport *t,uint32_t offset,
                           void *dst,uint32_t bytes) {
    assert(t && offset==0U && bytes==8U);
    uint64_t capacity=32768U;
    memcpy(dst,&capacity,8);
    return 0;
}
uint32_t nas_virtio_config32(const struct nas_virtio_transport *t,uint32_t off) {
    assert(t && off==20U);return 512U;
}
int nas_virtio_negotiate(const struct nas_virtio_transport *t,
                         uint64_t required,uint64_t optional,uint64_t *accepted) {
    assert(t && !required && (optional&BLK_F_RO));
    *accepted=BLK_F_BLK_SIZE | (scenario!=NO_FLUSH?BLK_F_FLUSH:0) |
              (scenario==READ_ONLY?BLK_F_RO:0);
    return 0;
}
int nas_virtio_activate(const struct nas_virtio_transport *t) {
    assert(t);return 0;
}
uint32_t nas_virtio_irq_ack(const struct nas_virtio_transport *t) {
    assert(t);return 0;
}
uint32_t nas_virtio_irq_ack_source(void *context) {
    assert(context);return NAS_VIRTIO_IRQ_USED_BUFFER;
}
int nas_irq_bind(struct nas_irq_action *action,struct nas_irq_route route,
                 nas_irq_source_ack_fn source,void *source_context,
                 nas_irq_notify_fn notify,void *notify_context) {
    assert(action && !irq && source==nas_virtio_irq_ack_source);
    assert(route.domain==NAS_IRQ_DOMAIN_RISCV_PLIC && route.hwirq==9U);
    *action=(struct nas_irq_action){.route=route,.source_ack=source,
         .source_context=source_context,.notify=notify,
         .notify_context=notify_context,.bound=true};
    irq=action;return 0;
}
int nas_irq_unbind(struct nas_irq_action *action) {
    if(action->bound) {assert(irq==action);irq=NULL;action->bound=false;}
    return 0;
}
uint64_t nas_arch_monotonic_ns(void) {ticks+=UINT64_C(1000000);return ticks;}
static void mock_reset(void *context) {assert(context);resets++;}
static uint32_t mock_ack(void *context) {assert(context);return 1U;}
int nas_vring_queue_init(struct nas_vring_queue *q,
                         const struct nas_virtio_transport *tr,
                         unsigned qid,unsigned count) {
    assert(q && tr && qid==0 && count==BLK_RING_DESC);
    q->transport=tr;q->active=true;return 0;
}
int nas_vring_queue_close(struct nas_vring_queue *q) {
    assert(q && q->active);
    q->active=false;q->transport->ops->reset(q->transport->context);
    return 0;
}
int nas_vring_queue_submit(struct nas_vring_queue *q,
                           const struct nas_vring_segment *segments,
                           unsigned n,uint16_t *head) {
    assert(q && q->active && !q->poisoned && segments && head && (n==2||n==3));
    assert(queue_count<64U);
    const unsigned id=queue_count++;
    struct pending_entry *entry=&pending[id];
    for(unsigned i=0;i<n;i++)entry->s[i]=segments[i];
    entry->n=n;entry->head=(uint16_t)(id*3U);
    *head=entry->head;
    q->inflight++;
    return 0;
}
/* Complete newest submitted first, independently of the submission order.
 * A real device is free to return these tags in any order. */
static void mock_hardware(void) {
    if(scenario==TIMEOUT || scenario==NO_IRQ)return;
    bool any=false;
    for(unsigned i=0;i<queue_count;i++) {
        struct pending_entry *entry=&pending[i];
        if(entry->done || entry->reclaimed)continue;
        const struct blk_hdr *header=(const void *)(uintptr_t)entry->s[0].bus_address;
        uint8_t *status=(uint8_t *)(uintptr_t)entry->s[entry->n-1U].bus_address;
        const uint32_t type=header->type;
        assert(header->reserved==0U);
        if(type==BLK_T_FLUSH) {
            assert(entry->n==2 && header->sector==0U);
            entry->wrote=1U;
        }else {
            assert(entry->n==3 && header->sector<32768U &&
                   entry->s[1].length==512U);
            const uint64_t offset=header->sector*512U;
            void *payload=(void *)(uintptr_t)entry->s[1].bus_address;
            if(type==BLK_T_IN) {
                assert(entry->s[1].device_writes);
                memcpy(payload,storage+offset,512U);
                entry->wrote=513U;
            } else {
                assert(type==BLK_T_OUT && !entry->s[1].device_writes);
                memcpy(storage+offset,payload,512U);
                entry->wrote=1U;
            }
        }
        *status=(scenario==BAD_STATUS)?1U:0U;
        if(scenario==BAD_USED)entry->wrote=999U;
        entry->done=true;
        any=true;
    }
    if(any && irq) {
        irq->notify(irq->notify_context,irq->source_ack(irq->source_context));
        notifications++;
    }
}
int nas_vring_queue_poll(struct nas_vring_queue *q,uint16_t *head,uint32_t *bytes) {
    assert(q && q->active && head && bytes);
    for(unsigned i=queue_count;i>0;i--) {
        struct pending_entry *entry=&pending[i-1U];
        if(entry->done) {
            entry->done=false;
            entry->reclaimed=true;
            *head=scenario==BAD_HEAD ? UINT16_MAX:entry->head;
            *bytes=entry->wrote;
            q->inflight--;
            ++completions;
            return 0;
        }
    }
    return -EAGAIN;
}
static enum scenario parse(const char *s) {
    if(!strcmp(s,"good"))return GOOD;
    if(!strcmp(s,"bad_used"))return BAD_USED;
    if(!strcmp(s,"bad_status"))return BAD_STATUS;
    if(!strcmp(s,"timeout"))return TIMEOUT;
    if(!strcmp(s,"no_irq"))return NO_IRQ;
    if(!strcmp(s,"alloc_fail"))return ALLOC_FAIL;
    if(!strcmp(s,"read_only"))return READ_ONLY;
    if(!strcmp(s,"no_flush"))return NO_FLUSH;
    if(!strcmp(s,"remove_busy"))return REMOVE_BUSY;
    if(!strcmp(s,"bad_head"))return BAD_HEAD;
    assert(!"unknown scenario");return GOOD;
}
static void expect_seed(void) {
    pattern(storage+16U*512U,"M178_HOST_SEED_A",7U);
    pattern(storage+17U*512U,"M178_HOST_SEED_B",19U);
}
int main(int argc,char **argv) {
    assert(argc==2);
    scenario=parse(argv[1]);
    static const struct nas_dma_provider dma={.coherent=true};
    static const struct nas_virtio_transport_ops ops={
        .reset=mock_reset,.read_ack_irq=mock_ack
    };
    static const struct nas_virtio_transport transport={
        .context=&storage[0],.ops=&ops,.dma=&dma
    };
    struct nas_bus_device dev={.bus=NAS_BUS_VIRTIO_MMIO,.device_id=VIRTIO_ID_BLOCK,
        .resource={.irq={.domain=NAS_IRQ_DOMAIN_RISCV_PLIC,.hwirq=9U}},
        .virtio=&transport};
    expect_seed();
    assert(nas_async_blk_is_fixture(&dev));
    assert(nas_async_blk_driver_register()==0);
    assert(registered_driver && registered_driver->match(&dev));
    const int rc=registered_driver->probe(&dev);
    if(scenario==ALLOC_FAIL || scenario==READ_ONLY || scenario==NO_FLUSH) {
        assert(rc<0 && allocs==frees && resets);
        printf("[M178 HOST ASYNC BLK NEGATIVE PASS] case=%s alloc=%u free=%u\n",
               argv[1],allocs,frees);
        return 0;
    }
    assert(!rc && nas_async_blk_count()==1U &&
           nas_async_blk_owns_device(&dev));
    if(scenario==NO_IRQ) {
        nas_async_blk_native_test_step();
        for(unsigned i=0;i<15;i++)nas_async_blk_service();
        assert(disk.submitted==2 && disk.completed==0 && !disk.deferred);
        puts("[M178 HOST ASYNC BLK NEGATIVE PASS] case=no_irq no_fake_poll");
        return 0;
    }
    if(scenario==TIMEOUT) {
        nas_async_blk_native_test_step();
        ticks=BLK_TIMEOUT_NS+UINT64_C(1000000);
        nas_async_blk_service();
        assert(disk.poisoned && resets && disk.requests.active==2);
        assert(test_a->state==NAS_BLOCK_REQ_DONE &&
               test_b->state==NAS_BLOCK_REQ_DONE);
        release_test_requests();
        assert(registered_driver->remove(&dev)==0 && allocs==frees);
        printf("[M178 HOST ASYNC BLK NEGATIVE PASS] case=timeout alloc=%u free=%u\n",
               allocs,frees);
        return 0;
    }
    if(scenario==REMOVE_BUSY) {
        nas_async_blk_native_test_step();
        assert(registered_driver->remove(&dev)==-EBUSY);
        mock_hardware();nas_async_blk_service();
        assert(finished(test_a) && finished(test_b));
        release_test_requests();
        assert(registered_driver->remove(&dev)==0 && allocs==frees);
        printf("[M178 HOST ASYNC BLK NEGATIVE PASS] case=remove_busy alloc=%u free=%u\n",
               allocs,frees);
        return 0;
    }
    if(scenario==BAD_USED || scenario==BAD_STATUS || scenario==BAD_HEAD) {
        nas_async_blk_native_test_step();
        mock_hardware();nas_async_blk_service();
        if(scenario==BAD_STATUS) {
            assert(finished(test_a) && finished(test_b));
            assert(test_a->result==-EIO && test_b->result==-EIO);
        }else {
            assert(disk.poisoned && resets);
            assert(finished(test_a) && finished(test_b));
            assert(test_a->result==-EIO && test_b->result==-EIO);
        }
        release_test_requests();
        assert(registered_driver->remove(&dev)==0 && allocs==frees);
        printf("[M178 HOST ASYNC BLK NEGATIVE PASS] case=%s alloc=%u free=%u\n",
               argv[1],allocs,frees);
        return 0;
    }
    assert(scenario==GOOD);
    for(unsigned i=0;i<50 && test_stage!=TEST_PASSED;i++) {
        nas_async_blk_native_test_step();
        mock_hardware();
        nas_async_blk_service();
    }
    assert(test_stage==TEST_PASSED && disk.submitted==7U &&
           disk.completed==7U && disk.max_inflight==2U && disk.irq_ack &&
           disk.deferred && !disk.fallback_poll &&
           disk.verified_reads==4U && disk.verified_writes==2U &&
           disk.flushes==1U && disk.requests.active==0U);
    uint8_t want[512];
    pattern(want,"M178_GUEST_WRITE_A",7U);
    assert(!memcmp(want,storage+32U*512U,512U));
    pattern(want,"M178_GUEST_WRITE_B",19U);
    assert(!memcmp(want,storage+33U*512U,512U));
    assert(registered_driver->remove(&dev)==0 && allocs==frees);
    printf("[M178 HOST ASYNC BLK PASS] out_of_order=%u alloc=%u free=%u\n",
           completions,allocs,frees);
    return 0;
}
