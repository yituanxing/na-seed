/* VirtIO 1.2 block request engine: one real PCI/MMIO class driver, one
 * split-ring, eight independent DMA payloads, hardware IRQ notification and
 * scheduler-context completion. The original U-Boot driver is unchanged;
 * only the explicitly provisioned second disk may bind here. */
#include "async_blk.h"
#include "../../kernel/drivers/core/irq.h"
#include "../../kernel/drivers/virtio/transport.h"
#include "../../kernel/drivers/port/dma.h"
#include "../../kernel/arch/arch.h"
#include "../../kernel/panic.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "../unikraft/split_queue.h"
#include <virtio/virtio_ids.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

extern void nas_print(const char *);
extern void nas_dec(uint64_t);

#define BLK_SLOTS NAS_BLOCK_REQUEST_SLOT_LIMIT
#define BLK_RING_DESC 32U
#define BLK_HEADER_BYTES 16U
#define BLK_STATUS_OFFSET 64U
#define BLK_F_RO (UINT64_C(1) << 5)
#define BLK_F_BLK_SIZE (UINT64_C(1) << 6)
#define BLK_F_FLUSH (UINT64_C(1) << 9)
#define BLK_T_IN 0U
#define BLK_T_OUT 1U
#define BLK_T_FLUSH 4U
#define BLK_S_OK 0U
#define BLK_TIMEOUT_NS UINT64_C(5000000000)
#define BLK_TEST_READ_A 16U
#define BLK_TEST_READ_B 17U
#define BLK_TEST_WRITE_A 32U
#define BLK_TEST_WRITE_B 33U

struct blk_hdr {
    uint32_t type, reserved;
    uint64_t sector;
};
_Static_assert(sizeof(struct blk_hdr)==BLK_HEADER_BYTES,"VirtIO block header ABI");

struct blk_slot {
    struct nas_dma_page header, payload;
    struct nas_block_request *req;
    uint16_t head;
    uint32_t request_generation;
};

struct async_block {
    struct nas_virtio_transport transport;
    struct nas_bus_device *owner;
    struct nas_vring_queue ring;
    struct nas_irq_action irq;
    struct nas_block_device geometry;
    struct nas_block_request_pool requests;
    struct blk_slot slots[BLK_SLOTS];
    uint64_t submitted, completed, irq_ack, deferred, fallback_poll;
    uint64_t verified_reads, verified_writes, flushes;
    uint32_t irq_pending;
    unsigned max_inflight;
    bool ready, poisoned, flush_supported, read_only, reported;
};
static struct async_block disk;
enum test_stage { TEST_SEEDED=0,TEST_SEED_PENDING,TEST_WRITE_PENDING,
                  TEST_FLUSH_PENDING,TEST_VERIFY_PENDING,TEST_PASSED };
static enum test_stage test_stage;
static struct nas_block_request *test_a,*test_b;

static uint32_t le32(uint32_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return value;
#else
    return __builtin_bswap32(value);
#endif
}
static uint64_t le64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return value;
#else
    return __builtin_bswap64(value);
#endif
}
static uint64_t fixture_capacity(const struct nas_virtio_transport *tr) {
    uint64_t result=0;
    if(!tr || nas_virtio_config_read(tr,0,&result,sizeof(result)))return 0;
    return le64(result);
}
bool nas_async_blk_is_fixture(const struct nas_bus_device *dev) {
    return dev && dev->device_id==VIRTIO_ID_BLOCK && dev->virtio &&
           fixture_capacity(dev->virtio)==NAS_ASYNC_BLK_FIXTURE_SECTORS;
}
static void irq_event(void *context,uint32_t source_status) {
    struct async_block *b=context;
    if(b && (source_status & (NAS_VIRTIO_IRQ_USED_BUFFER |
                             NAS_VIRTIO_IRQ_CONFIG_CHANGE))) {
        __atomic_fetch_add(&b->irq_ack,1U,__ATOMIC_RELAXED);
        __atomic_fetch_or(&b->irq_pending,source_status,__ATOMIC_RELEASE);
    }
    /* No virtqueue polling, allocation, callback or BlockRequest completion
     * in the device/controller hardware interrupt stack. */
}

static void reset_own_dma(struct async_block *b) {
    (void)nas_irq_unbind(&b->irq);
    if(b->ring.active)(void)nas_vring_queue_close(&b->ring);
    else if(b->transport.ops && b->transport.ops->reset)
        b->transport.ops->reset(b->transport.context);
    for(unsigned i=0;i<BLK_SLOTS;i++) {
        if(b->slots[i].header.cpu)
            (void)nas_dma_free_page(b->transport.dma,&b->slots[i].header);
        if(b->slots[i].payload.cpu)
            (void)nas_dma_free_page(b->transport.dma,&b->slots[i].payload);
    }
}
/* Poison is terminal for a hardware queue, not for a single valid I/O
 * status. First stop all DMA; then exactly once complete every pinned
 * request. The consumers still own their Request storage until release. */
static void halt_queue(struct async_block *b,int error) {
    if(!b || b->poisoned)return;
    b->poisoned=true;
    (void)nas_irq_unbind(&b->irq);
    if(b->ring.active)(void)nas_vring_queue_close(&b->ring);
    else if(b->transport.ops && b->transport.ops->reset)
        b->transport.ops->reset(b->transport.context);
    for(unsigned i=0;i<BLK_SLOTS;i++) {
        struct blk_slot *slot=&b->slots[i];
        if(!slot->req)continue;
        (void)nas_block_request_cancel(slot->req,error);
        slot->req=NULL;
    }
}

/* Caller-owned BlockRequest (including stable data) survives the submitter.
 * Every hardware request has two private DMA pages; no global mutable
 * header/status/payload can be overwritten by a second in-flight request. */
int nas_async_blk_submit(struct nas_block_request *r) {
    struct async_block *b=&disk;
    if(!b->ready || b->poisoned)return -ENODEV;
    if(!r || r->pool!=&b->requests || r->state!=NAS_BLOCK_REQ_READY)
        return -EINVAL;
    if(r->op==NAS_BLOCK_REQ_FLUSH && !b->flush_supported)return -EOPNOTSUPP;
    if(r->op==NAS_BLOCK_REQ_WRITE && b->read_only)return -EROFS;
    unsigned index=BLK_SLOTS;
    for(unsigned i=0;i<BLK_SLOTS;i++)
        if(!b->slots[i].req) {index=i;break;}
    if(index==BLK_SLOTS)return -ENOSPC;
    struct blk_slot *slot=&b->slots[index];
    const uint32_t command=r->op==NAS_BLOCK_REQ_READ ? BLK_T_IN :
                           r->op==NAS_BLOCK_REQ_WRITE ? BLK_T_OUT : BLK_T_FLUSH;
    const struct blk_hdr hdr={le32(command),0,
        le64(r->op==NAS_BLOCK_REQ_FLUSH ? 0 : r->sector*(b->geometry.sector_bytes/512U))};
    memset(slot->header.cpu,0,NAS_DMA_PAGE_BYTES);
    memcpy(slot->header.cpu,&hdr,sizeof(hdr));
    ((uint8_t *)slot->header.cpu)[BLK_STATUS_OFFSET]=0xffU;
    int rc=nas_dma_sync(b->transport.dma,&slot->header,0,
                        BLK_HEADER_BYTES,NAS_DMA_TO_DEVICE);
    if(rc)return rc;
    rc=nas_dma_sync(b->transport.dma,&slot->header,BLK_STATUS_OFFSET,
                    1U,NAS_DMA_FROM_DEVICE);
    if(rc)return rc;
    if(r->op==NAS_BLOCK_REQ_WRITE) {
        memcpy(slot->payload.cpu,r->payload,r->bytes);
        rc=nas_dma_sync(b->transport.dma,&slot->payload,0,r->bytes,
                        NAS_DMA_TO_DEVICE);
    } else if(r->op==NAS_BLOCK_REQ_READ) {
        rc=nas_dma_sync(b->transport.dma,&slot->payload,0,r->bytes,
                        NAS_DMA_FROM_DEVICE);
    }
    if(rc)return rc;
    struct nas_vring_segment segments[3];
    unsigned n=0;
    segments[n++]=(struct nas_vring_segment){slot->header.bus_address,
                                            BLK_HEADER_BYTES,false};
    if(r->op!=NAS_BLOCK_REQ_FLUSH)
        segments[n++]=(struct nas_vring_segment){slot->payload.bus_address,
                            r->bytes,r->op==NAS_BLOCK_REQ_READ};
    segments[n++]=(struct nas_vring_segment){slot->header.bus_address+
                             BLK_STATUS_OFFSET,1U,true};
    uint16_t head=0;
    rc=nas_vring_queue_submit(&b->ring,segments,n,&head);
    if(rc) {
        if(b->ring.poisoned)halt_queue(b,-EIO);
        return rc;
    }
    /* The descriptor chain now belongs to hardware. Record its Request
     * ownership BEFORE any further fallible operation, so fail-closed
     * reset/cancel can never lose a published DMA request. */
    slot->req=r;
    slot->head=head;
    slot->request_generation=r->generation;
    rc=nas_block_request_mark_submitted(r);
    if(rc) {halt_queue(b,-EIO);return rc;}
    ++b->submitted;
    if(b->ring.inflight>b->max_inflight)b->max_inflight=b->ring.inflight;
    return 0;
}

/* Deferred drain is invoked on the native scheduler stack. Device ACK is
 * the only reason to poll the ring in normal execution: timer-driven
 * speculative polling is deliberately forbidden for the 16MiB fixture. */
void nas_async_blk_service(void) {
    struct async_block *b=&disk;
    if(!b->ready || b->poisoned)return;
    uint32_t signal=__atomic_exchange_n(&b->irq_pending,0U,__ATOMIC_ACQUIRE);
    if(signal & NAS_VIRTIO_IRQ_USED_BUFFER) {
        ++b->deferred;
        for(unsigned budget=0;budget<BLK_SLOTS;budget++) {
            uint16_t head=0;
            uint32_t bytes=0;
            int rc=nas_vring_queue_poll(&b->ring,&head,&bytes);
            if(rc==-EAGAIN)break;
            if(rc) {halt_queue(b,-EIO);return;}
            struct blk_slot *slot=NULL;
            for(unsigned i=0;i<BLK_SLOTS;i++)
                if(b->slots[i].req && b->slots[i].head==head) {
                    slot=&b->slots[i];break;
                }
            if(!slot) {halt_queue(b,-EIO);return;}
            struct nas_block_request *r=slot->req;
            uint32_t expected=r->op==NAS_BLOCK_REQ_READ ? r->bytes+1U : 1U;
            if(bytes!=expected) {halt_queue(b,-EIO);return;}
            rc=nas_dma_sync(b->transport.dma,&slot->header,
                            BLK_STATUS_OFFSET,1U,NAS_DMA_FROM_DEVICE);
            if(rc) {halt_queue(b,-EIO);return;}
            const uint8_t status=((const uint8_t *)slot->header.cpu)[BLK_STATUS_OFFSET];
            if(status==BLK_S_OK && r->op==NAS_BLOCK_REQ_READ) {
                rc=nas_dma_sync(b->transport.dma,&slot->payload,0,r->bytes,
                                NAS_DMA_FROM_DEVICE);
                if(rc) {halt_queue(b,-EIO);return;}
                memcpy(r->payload,slot->payload.cpu,r->bytes);
            }
            slot->req=NULL;
            rc=nas_block_request_complete_checked(r,slot->request_generation,
                                                   status==BLK_S_OK?0:-EIO);
            if(rc) {halt_queue(b,-EIO);return;}
            ++b->completed;
        }
    }
    const uint64_t now=nas_arch_monotonic_ns();
    for(unsigned i=0;i<BLK_SLOTS;i++) {
        struct nas_block_request *r=b->slots[i].req;
        if(r && nas_block_request_deadline_expired(r,now)) {
            halt_queue(b,-ETIMEDOUT);
            return;
        }
    }
}

static int setup(struct async_block *b,struct nas_bus_device *dev) {
    b->owner=dev;
    b->transport=*dev->virtio;
    if(!dev->resource.irq.domain || !b->transport.ops ||
       !b->transport.ops->read_ack_irq)return -EOPNOTSUPP;
    uint64_t features=0;
    int rc=nas_virtio_negotiate(&b->transport,0,
              BLK_F_RO|BLK_F_BLK_SIZE|BLK_F_FLUSH,&features);
    if(rc)return rc;
    b->read_only=(features&BLK_F_RO)!=0;
    b->flush_supported=(features&BLK_F_FLUSH)!=0;
    if(b->read_only || !b->flush_supported)return -EOPNOTSUPP;
    uint32_t sector_bytes=(features&BLK_F_BLK_SIZE) ?
                           nas_virtio_config32(&b->transport,20U) : 512U;
    if(sector_bytes!=512U)return -EOPNOTSUPP;
    b->geometry=(struct nas_block_device){
        .name="async-fixture",.sectors=NAS_ASYNC_BLK_FIXTURE_SECTORS,
        .sector_bytes=sector_bytes,.max_sectors_per_io=NAS_DMA_PAGE_BYTES/sector_bytes,
        .read_only=b->read_only
    };
    rc=nas_block_request_pool_init(&b->requests,&b->geometry);
    if(rc)return rc;
    for(unsigned i=0;i<BLK_SLOTS;i++) {
        rc=nas_dma_alloc_page(b->transport.dma,&b->slots[i].header);
        if(rc)return rc;
        rc=nas_dma_alloc_page(b->transport.dma,&b->slots[i].payload);
        if(rc)return rc;
    }
    rc=nas_vring_queue_init(&b->ring,&b->transport,0U,BLK_RING_DESC);
    if(rc)return rc;
    (void)nas_virtio_irq_ack(&b->transport);
    rc=nas_irq_bind(&b->irq,dev->resource.irq,
                    nas_virtio_irq_ack_source,&b->transport,irq_event,b);
    if(rc)return rc;
    rc=nas_virtio_activate(&b->transport);
    if(rc)return rc;
    b->ready=true;
    return 0;
}

static int probe(struct nas_bus_device *dev) {
    if(disk.ready || disk.owner)return -EBUSY;
    memset(&disk,0,sizeof(disk));
    test_stage=TEST_SEEDED;
    test_a=NULL;
    test_b=NULL;
    int rc=setup(&disk,dev);
    if(rc) {
        reset_own_dma(&disk);
        memset(&disk,0,sizeof(disk));
    }
    return rc;
}
static int remove_device(struct nas_bus_device *dev) {
    if(!disk.owner || disk.owner!=dev)return -ENODEV;
    if(!nas_block_request_pool_idle(&disk.requests))return -EBUSY;
    reset_own_dma(&disk);
    memset(&disk,0,sizeof(disk));
    test_stage=TEST_SEEDED;
    test_a=NULL;
    test_b=NULL;
    return 0;
}
static const struct nas_bus_driver driver={
    .name="virtio-blk-async-fixture",.bus_mask=NAS_BUS_MASK_ALL,
    .match=nas_async_blk_is_fixture,.probe=probe,.remove=remove_device
};
int nas_async_blk_driver_register(void) {
    return nas_bus_driver_register(&driver);
}
unsigned nas_async_blk_count(void) {return disk.ready?1U:0U;}
bool nas_async_blk_owns_device(const struct nas_bus_device *dev) {
    return dev && dev==disk.owner && disk.ready;
}
void nas_async_blk_get_stats(struct nas_async_blk_stats *out) {
    if(!out)return;
    *out=(struct nas_async_blk_stats){
        .submitted=disk.submitted,.completed=disk.completed,
        .irq_ack=__atomic_load_n(&disk.irq_ack,__ATOMIC_RELAXED),
        .deferred=disk.deferred,.fallback_poll=disk.fallback_poll,
        .verified_reads=disk.verified_reads,.verified_writes=disk.verified_writes,
        .flushes=disk.flushes,.max_inflight=disk.max_inflight
    };
}

/* Proof is genuine guest/host storage, not merely a marker: host seeds
 * sectors 16/17 in the raw 16MiB fixture, guest compares all 512 bytes,
 * writes independent patterns to 32/33, flushes, then reads both back.
 * The native oracle independently inspects persistent host file bytes. */
static void pattern(uint8_t *out,const char *header,unsigned salt) {
    for(unsigned i=0;i<512U;i++)out[i]=(uint8_t)((i*13U+salt)&255U);
    for(unsigned i=0;header[i];i++)out[i]=(uint8_t)header[i];
    out[strlen(header)]=0;
}
static bool equal512(const uint8_t *a,const uint8_t *b) {
    for(unsigned i=0;i<512U;i++)if(a[i]!=b[i])return false;
    return true;
}
static void submit_or_fail(struct nas_block_request **slot,
                           enum nas_block_request_op op,uint64_t lba,
                           const void *write_bytes) {
    struct nas_block_request *r=NULL;
    const uint64_t expiry=nas_arch_monotonic_ns()+BLK_TIMEOUT_NS;
    int rc=nas_block_request_acquire(&disk.requests,op,lba,
                    op==NAS_BLOCK_REQ_FLUSH?0U:1U,write_bytes,
                    expiry,NULL,NULL,&r);
    if(rc || !r)nas_fail("M178 BlockRequest acquire failed");
    rc=nas_async_blk_submit(r);
    if(rc)nas_fail("M178 BlockRequest hardware submit failed");
    *slot=r;
}
static bool finished(struct nas_block_request *r) {
    return r && r->state==NAS_BLOCK_REQ_DONE;
}
static void expect_completion(struct nas_block_request *r) {
    if(!finished(r) || r->result)nas_fail("M178 BlockRequest hardware completion failed");
}
static void release_test_requests(void) {
    if(test_a) {
        if(nas_block_request_release(test_a))nas_fail("M178 BlockRequest release A");
        test_a=NULL;
    }
    if(test_b) {
        if(nas_block_request_release(test_b))nas_fail("M178 BlockRequest release B");
        test_b=NULL;
    }
}
void nas_async_blk_native_test_step(void) {
    if(!disk.ready || test_stage==TEST_PASSED)return;
    uint8_t expected[512U];
    switch(test_stage) {
    case TEST_SEEDED:
        submit_or_fail(&test_a,NAS_BLOCK_REQ_READ,BLK_TEST_READ_A,NULL);
        submit_or_fail(&test_b,NAS_BLOCK_REQ_READ,BLK_TEST_READ_B,NULL);
        test_stage=TEST_SEED_PENDING;
        break;
    case TEST_SEED_PENDING:
        if(!finished(test_a) || !finished(test_b))break;
        expect_completion(test_a);expect_completion(test_b);
        pattern(expected,"M178_HOST_SEED_A",7U);
        if(!equal512(test_a->payload,expected))nas_fail("M178 host seed A mismatch");
        pattern(expected,"M178_HOST_SEED_B",19U);
        if(!equal512(test_b->payload,expected))nas_fail("M178 host seed B mismatch");
        disk.verified_reads+=2U;
        release_test_requests();
        pattern(expected,"M178_GUEST_WRITE_A",7U);
        submit_or_fail(&test_a,NAS_BLOCK_REQ_WRITE,BLK_TEST_WRITE_A,expected);
        pattern(expected,"M178_GUEST_WRITE_B",19U);
        submit_or_fail(&test_b,NAS_BLOCK_REQ_WRITE,BLK_TEST_WRITE_B,expected);
        test_stage=TEST_WRITE_PENDING;
        break;
    case TEST_WRITE_PENDING:
        if(!finished(test_a) || !finished(test_b))break;
        expect_completion(test_a);expect_completion(test_b);
        disk.verified_writes+=2U;
        release_test_requests();
        submit_or_fail(&test_a,NAS_BLOCK_REQ_FLUSH,0U,NULL);
        test_stage=TEST_FLUSH_PENDING;
        break;
    case TEST_FLUSH_PENDING:
        if(!finished(test_a))break;
        expect_completion(test_a);
        ++disk.flushes;
        release_test_requests();
        submit_or_fail(&test_a,NAS_BLOCK_REQ_READ,BLK_TEST_WRITE_A,NULL);
        submit_or_fail(&test_b,NAS_BLOCK_REQ_READ,BLK_TEST_WRITE_B,NULL);
        test_stage=TEST_VERIFY_PENDING;
        break;
    case TEST_VERIFY_PENDING:
        if(!finished(test_a) || !finished(test_b))break;
        expect_completion(test_a);expect_completion(test_b);
        pattern(expected,"M178_GUEST_WRITE_A",7U);
        if(!equal512(test_a->payload,expected))nas_fail("M178 write A readback mismatch");
        pattern(expected,"M178_GUEST_WRITE_B",19U);
        if(!equal512(test_b->payload,expected))nas_fail("M178 write B readback mismatch");
        disk.verified_reads+=2U;
        release_test_requests();
        if(disk.submitted!=7U || disk.completed!=7U ||
           disk.max_inflight<2U || !disk.irq_ack || !disk.deferred ||
           disk.fallback_poll || disk.verified_reads!=4U ||
           disk.verified_writes!=2U || disk.flushes!=1U)
            nas_fail("M178 invalid physical IRQ/DMA proof counters");
        nas_print("[M178 ASYNC BLK STATS] submitted=");nas_dec(disk.submitted);
        nas_print(" completed=");nas_dec(disk.completed);
        nas_print(" max_inflight=");nas_dec(disk.max_inflight);
        nas_print(" irq_ack=");nas_dec(disk.irq_ack);
        nas_print(" deferred=");nas_dec(disk.deferred);
        nas_print(" fallback_poll=");nas_dec(disk.fallback_poll);
        nas_print(" verified_reads=");nas_dec(disk.verified_reads);
        nas_print(" verified_writes=");nas_dec(disk.verified_writes);
        nas_print(" flushes=");nas_dec(disk.flushes);nas_print("\n");
        nas_print("[M178 ASYNC BLK NATIVE PASS]\n");
        test_stage=TEST_PASSED;
        break;
    case TEST_PASSED: break;
    }
}
