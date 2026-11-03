/* Standalone mock VirtIO-GPU (real class-driver source included verbatim).
 * This is not a synthetic guest visual oracle: the production QEMU matrix
 * separately proves physical scanout, distinct frames and device IRQs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define NAS_ENABLE_VIRTIO_GPU 1
#include "../kernel/drivers/display/virtio_gpu.c"

enum failure {
    GOOD, BAD_RESPONSE, BAD_FENCE, SHORT_REPLY, MISSING_SCANOUT,
    BAD_CREATE, BAD_ATTACH, BAD_SCANOUT, TIMEOUT, ALLOC_FAIL,
    NO_IRQ_SOURCE, NO_PRIMARY, RUNTIME_FENCE, RUNTIME_TIMEOUT,
    RUNTIME_DIRTY_EPOCH
};
static enum failure failure;
static const struct nas_bus_driver *registered_driver;
static struct nas_display_device *primary;
static struct nas_irq_action *bound_irq;
static unsigned allocs, frees, resets, syncs, commands, registered;
static unsigned scanout, transfers, flushes;
static uint32_t last_transfer_y, last_transfer_height;
static uint64_t last_transfer_offset;
static struct gpu_hdr *last_reply;
static uint16_t head;
static uint32_t response_bytes;
static uint64_t ticks;
static uint32_t submitted_types[32];
static uint32_t original_alloc_count;
static uint32_t irq_status;
static unsigned polls;

void nas_print(const char *str) { fputs(str,stdout); }
void nas_dec(uint64_t n) { printf("%llu",(unsigned long long)n); }

int nas_bus_driver_register(const struct nas_bus_driver *driver) {
    assert(driver && !registered_driver);
    registered_driver=driver;
    ++registered;
    return 0;
}
bool nas_display_boot_takeover(struct nas_display_device *display) {
    if (failure==NO_PRIMARY) return false;
    assert(!primary);
    primary=display;
    return true;
}
bool nas_display_register_primary(struct nas_display_device *display) {
    if (failure==NO_PRIMARY) return false;
    assert(!primary);
    primary=display;
    return true;
}

int nas_dma_alloc_page(const struct nas_dma_provider *provider,
                       struct nas_dma_page *page) {
    assert(provider && page && !page->cpu);
    if (failure==ALLOC_FAIL && allocs==470U) return -ENOMEM;
    void *memory=aligned_alloc(NAS_DMA_PAGE_BYTES,NAS_DMA_PAGE_BYTES);
    assert(memory);
    memset(memory,0,NAS_DMA_PAGE_BYTES);
    *page=(struct nas_dma_page){.cpu=memory,.bus_address=(uintptr_t)memory,
                                .bytes=NAS_DMA_PAGE_BYTES};
    ++allocs;
    return 0;
}
int nas_dma_free_page(const struct nas_dma_provider *provider,
                      struct nas_dma_page *page) {
    assert(provider && page && page->cpu);
    free(page->cpu);
    memset(page,0,sizeof(*page));
    ++frees;
    return 0;
}
int nas_dma_sync(const struct nas_dma_provider *provider,
                 struct nas_dma_page *page,uint32_t offset,uint32_t bytes,
                 enum nas_dma_direction direction) {
    assert(provider && page && page->cpu && offset<=NAS_DMA_PAGE_BYTES);
    assert(bytes<=NAS_DMA_PAGE_BYTES-offset);
    assert(direction==NAS_DMA_TO_DEVICE || direction==NAS_DMA_FROM_DEVICE);
    ++syncs;
    return 0;
}
int nas_virtio_negotiate(const struct nas_virtio_transport *t,
                         uint64_t required,uint64_t optional,uint64_t *accepted) {
    assert(t && accepted && !required && !optional);
    *accepted=NAS_VIRTIO_F_VERSION_1;
    return 0;
}
uint32_t nas_virtio_config32(const struct nas_virtio_transport *t,uint32_t offset) {
    assert(t && offset==8U);
    return 1U;
}
int nas_virtio_activate(const struct nas_virtio_transport *t) {
    assert(t);
    return 0;
}
uint32_t nas_virtio_irq_ack(const struct nas_virtio_transport *t) {
    assert(t);
    return 0;
}
uint32_t nas_virtio_irq_ack_source(void *context) {
    assert(context);
    const uint32_t status=irq_status;
    irq_status=0U;
    return status;
}
int nas_irq_bind(struct nas_irq_action *action,struct nas_irq_route route,
                 nas_irq_source_ack_fn source,void *source_context,
                 nas_irq_notify_fn notify,void *notify_context) {
    assert(action && source && source_context && notify && notify_context);
    assert(!bound_irq && route.domain==NAS_IRQ_DOMAIN_RISCV_PLIC && route.hwirq==9);
    assert(source==nas_virtio_irq_ack_source);
    *action=(struct nas_irq_action){.route=route,.source_ack=source,
        .source_context=source_context,.notify=notify,
        .notify_context=notify_context,.bound=true};
    bound_irq=action;
    return 0;
}
int nas_irq_unbind(struct nas_irq_action *action) {
    if (action->bound) {
        assert(bound_irq==action);
        bound_irq=NULL;
        action->bound=false;
    }
    return 0;
}
uint64_t nas_arch_monotonic_ns(void) {
    ticks+=UINT64_C(700000000);
    return ticks;
}
static void hardware_reset(void *opaque) {
    assert(opaque);
    ++resets;
}
static uint32_t hardware_ack(void *opaque) {
    assert(opaque);
    return NAS_VIRTIO_IRQ_USED_BUFFER;
}

int nas_vring_queue_init(struct nas_vring_queue *queue,
                         const struct nas_virtio_transport *transport,
                         unsigned qid,unsigned descs) {
    assert(queue && transport && qid==0 && descs==16);
    queue->transport=transport;
    queue->active=true;
    return 0;
}
int nas_vring_queue_close(struct nas_vring_queue *queue) {
    assert(queue && queue->active);
    queue->active=false;
    queue->transport->ops->reset(queue->transport->context);
    return 0;
}
static void check_attach(const struct nas_vring_segment *segments,unsigned count) {
    assert(count==4U);
    assert(segments[1].length==NAS_DMA_PAGE_BYTES);
    assert(segments[2].length==GPU_PAGES*GPU_ENTRY_BYTES-NAS_DMA_PAGE_BYTES);
    const struct gpu_attach *a=(const void *)(uintptr_t)segments[0].bus_address;
    assert(a->id==GPU_RESOURCE_ID && a->count==GPU_PAGES);
    const struct gpu_entry *first=(const void *)(uintptr_t)segments[1].bus_address;
    const struct gpu_entry *last=(const void *)(uintptr_t)segments[2].bus_address;
    assert(first[0].address==gpu.pixels[0].bus_address);
    assert(first[0].length==NAS_DMA_PAGE_BYTES);
    assert(last[GPU_PAGES-NAS_DMA_PAGE_BYTES/GPU_ENTRY_BYTES-1].address==
           gpu.pixels[GPU_PAGES-1].bus_address);
    assert(last[GPU_PAGES-NAS_DMA_PAGE_BYTES/GPU_ENTRY_BYTES-1].length==
           GPU_BYTES%NAS_DMA_PAGE_BYTES);
}
int nas_vring_queue_submit(struct nas_vring_queue *queue,
                           const struct nas_vring_segment *segments,
                           unsigned count,uint16_t *out_head) {
    assert(queue && queue->active && segments && out_head && count>=2U);
    assert(commands<sizeof(submitted_types)/sizeof(submitted_types[0]));
    const struct gpu_hdr *request=(const void *)(uintptr_t)segments[0].bus_address;
    const uint32_t type=request->type;
    submitted_types[commands++]=type;
    assert((request->flags&GPU_FENCE) && request->fence_id==commands);
    assert(segments[count-1].device_writes);
    assert(segments[count-1].length==sizeof(struct gpu_resp_info));
    struct gpu_resp_info *answer=(void *)(uintptr_t)segments[count-1].bus_address;
    answer->hdr=(struct gpu_hdr){.type=GPU_RESP_OK_NODATA,.flags=GPU_FENCE,
                                  .fence_id=request->fence_id};
    response_bytes=sizeof(struct gpu_hdr);
    if (type==GPU_CMD_GET_DISPLAY_INFO) {
        assert(count==2U);
        answer->hdr.type=GPU_RESP_OK_DISPLAY_INFO;
        answer->modes[0]=(struct gpu_mode){.r={0,0,1024,768},
                                             .enabled=failure!=MISSING_SCANOUT};
        response_bytes=sizeof(*answer);
    } else if (type==GPU_CMD_RESOURCE_CREATE_2D) {
        assert(count==2U);
        const struct gpu_create *create=(const void *)request;
        assert(create->id==1 && create->format==2);
        assert(create->width==800 && create->height==600);
        if (failure==BAD_CREATE)answer->hdr.type=GPU_RESP_OK_DISPLAY_INFO;
    } else if (type==GPU_CMD_RESOURCE_ATTACH_BACKING) {
        check_attach(segments,count);
        if (failure==BAD_ATTACH)answer->hdr.type=GPU_RESP_OK_DISPLAY_INFO;
    } else if (type==GPU_CMD_SET_SCANOUT) {
        assert(count==2U);
        const struct gpu_scanout *s=(const void *)request;
        assert(s->id==GPU_RESOURCE_ID && s->scanout==0);
        assert(s->r.width==800 && s->r.height==600);
        ++scanout;
        if (failure==BAD_SCANOUT)answer->hdr.type=GPU_RESP_OK_DISPLAY_INFO;
    } else if (type==GPU_CMD_TRANSFER_TO_HOST_2D) {
        const struct gpu_transfer *t=(const void *)request;
        assert(count==2U && t->id==GPU_RESOURCE_ID);
        assert(t->r.x==0 && t->r.width==800 && t->r.height);
        assert(t->offset==(uint64_t)t->r.y*GPU_PITCH);
        last_transfer_y=t->r.y;
        last_transfer_height=t->r.height;
        last_transfer_offset=t->offset;
        ++transfers;
    } else if (type==GPU_CMD_RESOURCE_FLUSH) {
        assert(count==2U);
        const struct gpu_flush *f=(const void *)request;
        assert(f->r.y==last_transfer_y && f->r.height==last_transfer_height);
        ++flushes;
    } else assert(!"unexpected GPU command");
    if (failure==BAD_RESPONSE)answer->hdr.type=GPU_RESP_OK_NODATA+99U;
    if (failure==BAD_FENCE)answer->hdr.fence_id++;
    if (failure==RUNTIME_FENCE && type==GPU_CMD_TRANSFER_TO_HOST_2D)
        answer->hdr.fence_id++;
    if (failure==SHORT_REPLY)response_bytes=8U;
    last_reply=&answer->hdr;
    *out_head=head=0U;
    return 0;
}
int nas_vring_queue_poll(struct nas_vring_queue *queue,uint16_t *out_head,
                         uint32_t *bytes) {
    assert(queue && out_head && bytes && last_reply);
    ++polls;
    if (failure==TIMEOUT)return -EAGAIN;
    *out_head=head;
    *bytes=response_bytes;
    return 0;
}

static void deliver_gpu_completion(void) {
    assert(bound_irq && gpu.runtime_phase!=GPU_RUNTIME_IDLE);
    irq_status=NAS_VIRTIO_IRQ_USED_BUFFER;
    const uint32_t status=bound_irq->source_ack(bound_irq->source_context);
    assert(status==NAS_VIRTIO_IRQ_USED_BUFFER && !irq_status);
    bound_irq->notify(bound_irq->notify_context,status);
    assert(gpu.display.ops->present(&gpu.display)==0);
}

static enum failure parse_mode(const char *name) {
    if (!strcmp(name,"good"))return GOOD;
    if (!strcmp(name,"bad_response"))return BAD_RESPONSE;
    if (!strcmp(name,"bad_fence"))return BAD_FENCE;
    if (!strcmp(name,"short_reply"))return SHORT_REPLY;
    if (!strcmp(name,"missing_scanout"))return MISSING_SCANOUT;
    if (!strcmp(name,"bad_create"))return BAD_CREATE;
    if (!strcmp(name,"bad_attach"))return BAD_ATTACH;
    if (!strcmp(name,"bad_scanout"))return BAD_SCANOUT;
    if (!strcmp(name,"timeout"))return TIMEOUT;
    if (!strcmp(name,"alloc_fail"))return ALLOC_FAIL;
    if (!strcmp(name,"no_irq_source"))return NO_IRQ_SOURCE;
    if (!strcmp(name,"no_primary"))return NO_PRIMARY;
    if (!strcmp(name,"runtime_fence"))return RUNTIME_FENCE;
    if (!strcmp(name,"runtime_timeout"))return RUNTIME_TIMEOUT;
    if (!strcmp(name,"runtime_dirty_epoch"))return RUNTIME_DIRTY_EPOCH;
    fprintf(stderr,"unknown fixture: %s\n",name);
    exit(2);
}
int main(int argc,char **argv) {
    assert(argc==2);
    failure=parse_mode(argv[1]);
    static const struct nas_dma_provider dma={0};
    static const struct nas_virtio_transport_ops ops={
        .reset=hardware_reset,.read_ack_irq=hardware_ack
    };
    struct nas_virtio_transport_ops broken_ops=ops;
    if (failure==NO_IRQ_SOURCE)broken_ops.read_ack_irq=NULL;
    const struct nas_virtio_transport transport={
        .context=&original_alloc_count,.ops=&broken_ops,.dma=&dma
    };
    struct nas_bus_device device={
        .bus=NAS_BUS_VIRTIO_MMIO,.device_id=GPU_ID,
        .resource={.irq={NAS_IRQ_DOMAIN_RISCV_PLIC,9}},.virtio=&transport
    };
    assert(nas_virtio_gpu_driver_register()==0 && registered==1U);
    assert(registered_driver && registered_driver->match(&device));
    const int result=registered_driver->probe(&device);
    if (failure!=GOOD && failure!=RUNTIME_FENCE &&
        failure!=RUNTIME_TIMEOUT && failure!=RUNTIME_DIRTY_EPOCH) {
        assert(result<0 && !gpu.claimed && !primary && !bound_irq);
        assert(allocs==frees && !nas_virtio_gpu_count());
        assert(resets>=1U);
        printf("[M177 HOST GPU NEGATIVE PASS] case=%s alloc=%u free=%u\n",
               argv[1],allocs,frees);
        return 0;
    }
    assert(result==0 && primary==&gpu.display && scanout==1U);
    assert(nas_virtio_gpu_count()==1U && allocs==GPU_PAGES+GPU_ENTRY_PAGES+2U);
    assert(commands==4U && gpu.submitted==4U && gpu.completed==4U);
    assert(bound_irq && bound_irq->bound && gpu.dirty);
    assert(bound_irq->source_ack(bound_irq->source_context)==0U);
    assert(gpu.irq_acks==0U);
    assert(registered_driver->probe(&device)==-ENODEV);
    assert(gpu.display.mode.width==800 && gpu.display.mode.height==600);
    assert(gpu.display.mode.pitch==3200 && gpu.display.mode.bpp==32);
    if (failure==RUNTIME_FENCE) {
        assert(gpu.display.ops->present(&gpu.display)==0);
        assert(gpu.runtime_phase==GPU_RUNTIME_TRANSFER);
        irq_status=NAS_VIRTIO_IRQ_USED_BUFFER;
        const uint32_t status=bound_irq->source_ack(bound_irq->source_context);
        bound_irq->notify(bound_irq->notify_context,status);
        assert(gpu.display.ops->present(&gpu.display)==-EIO);
        assert(gpu.stalled && gpu.dirty);
        const unsigned sent=commands;
        assert(gpu.display.ops->present(&gpu.display)==-EIO);
        assert(commands==sent);
        assert(!gpu.reported && gpu.transfers==0U);
        printf("[M177 HOST GPU RUNTIME FAIL CLOSED PASS] pinned=%u pending=%u\n",
               allocs,commands);
        return 0;
    }
    if (failure==RUNTIME_TIMEOUT) {
        assert(gpu.display.ops->present(&gpu.display)==0);
        const unsigned submitted=commands;
        const unsigned polled=polls;
        int rc=0;
        for(unsigned n=0;n<8U && !rc;n++)
            rc=gpu.display.ops->present(&gpu.display);
        assert(rc==-ETIMEDOUT && gpu.stalled && gpu.dirty);
        assert(commands==submitted && polls==polled);
        assert(allocs==473U && !frees);
        puts("[M181 GPU IRQ TIMEOUT PASS] no polling, no DMA reuse");
        return 0;
    }
    if (failure==RUNTIME_DIRTY_EPOCH) {
        assert(gpu.display.ops->present(&gpu.display)==0);
        uint8_t pixel[8]={1,2,3,4,5,6,7,8};
        assert(gpu.display.ops->write_at(&gpu.display,GPU_PITCH*7U,
                                         pixel,sizeof(pixel))==sizeof(pixel));
        assert(gpu.dirty && gpu.dirty_y0==7U);
        deliver_gpu_completion(); /* First transfer; old epoch flush queued. */
        assert(gpu.dirty && gpu.dirty_y0==7U);
        deliver_gpu_completion(); /* Old flush; new transfer submitted. */
        assert(gpu.runtime_phase==GPU_RUNTIME_TRANSFER &&
               last_transfer_y==7U && last_transfer_height==1U);
        deliver_gpu_completion();
        deliver_gpu_completion();
        assert(!gpu.dirty && gpu.runtime_phase==GPU_RUNTIME_IDLE);
        assert(gpu.transfers==2U && gpu.flushes==2U);
        puts("[M181 GPU DIRTY EPOCH PASS] writes during old DMA re-presented");
        return 0;
    }
    /* Initial zero-filled resource is a real 800x600 TRANSFER+FLUSH. */
    assert(gpu.display.ops->present(&gpu.display)==0);
    assert(commands==5 && transfers==1 && flushes==0);
    const unsigned before_poll=polls;
    assert(gpu.display.ops->present(&gpu.display)==0 && polls==before_poll);
    deliver_gpu_completion();
    assert(commands==6 && transfers==1 && flushes==1);
    deliver_gpu_completion();
    assert(!gpu.dirty && last_transfer_y==0 && last_transfer_height==600);
    assert(gpu.display.ops->present(&gpu.display)==0 && commands==6);
    uint8_t bytes[1600],out[1600];
    memset(bytes,0x5a,sizeof(bytes));
    memset(out,0,sizeof(out));
    for (unsigned frame=0;frame<4;frame++) {
        const uint64_t offset=(uint64_t)(frame*133U+10U)*GPU_PITCH+2500;
        assert(gpu.display.ops->write_at(&gpu.display,offset,bytes,sizeof(bytes))
               ==sizeof(bytes));
        assert(gpu.display.ops->read_at(&gpu.display,offset,out,sizeof(out))
               ==sizeof(out));
        assert(memcmp(out,bytes,sizeof(out))==0);
        assert(gpu.dirty);
        assert(gpu.display.ops->present(&gpu.display)==0);
        assert(last_transfer_y==frame*133U+10U);
        assert(last_transfer_height==2U);
        assert(last_transfer_offset==(uint64_t)last_transfer_y*GPU_PITCH);
        assert(!gpu.dirty && gpu.runtime_phase==GPU_RUNTIME_TRANSFER);
        deliver_gpu_completion();
        deliver_gpu_completion();
    }
    assert(commands==14 && transfers==5 && flushes==5);
    assert(gpu.display.ops->write_at(&gpu.display,GPU_BYTES-8U,bytes,16U)==0);
    assert(gpu.display.ops->read_at(&gpu.display,GPU_BYTES-8U,out,16U)==0);
    struct nas_virtio_gpu_stats stats;
    nas_virtio_gpu_get_stats(&stats);
    assert(stats.width==800 && stats.height==600 && stats.backing_pages==469);
    assert(stats.scanout_id==0 && stats.submitted==14 && stats.completed==14);
    assert(stats.transfers==5 && stats.flushes==5 && stats.irq_acks==10);
    /* A valid one-shot native stats record can be emitted with exactly one
     * newer transfer in flight: M177 must not require equality here. */
    assert(gpu.display.ops->write_at(&gpu.display,GPU_PITCH*17U,
                                     bytes,sizeof(bytes))==sizeof(bytes));
    assert(gpu.display.ops->present(&gpu.display)==0);
    assert(gpu.runtime_phase==GPU_RUNTIME_TRANSFER &&
           gpu.submitted==15U && gpu.completed==14U &&
           gpu.transfers==5U && gpu.flushes==5U);
    nas_virtio_gpu_report();
    assert(gpu.reported);
    nas_virtio_gpu_report();
    assert(gpu.reported);
    deliver_gpu_completion();
    deliver_gpu_completion();
    assert(gpu.runtime_phase==GPU_RUNTIME_IDLE &&
           gpu.submitted==16U && gpu.completed==16U &&
           gpu.transfers==6U && gpu.flushes==6U);
    assert(syncs>100 && allocs==473U && frees==0U);
    printf("[M177 HOST GPU PROTOCOL PASS] 469 pages across 2 attach SG pages,"
           " GET/CREATE/ATTACH/SCANOUT, 5 fenced TRANSFER/FLUSH,"
           " row offset, dirty coalescing, exact DMA lifetime\n");
    return 0;
}
