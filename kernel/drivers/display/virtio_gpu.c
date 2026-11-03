/* VirtIO 1.2 GPU, unaccelerated 2D scanout. One reusable class driver over
 * the existing PCI/MMIO transport, IRQ, DMA and Unikraft split-queue layers.
 * Protocol definitions follow include/uapi/linux/virtio_gpu.h; the Linux
 * virtgpu_vq/plane/object split informs command and backing ownership.
 * This deliberately does not implement a second framebuffer or DRM ABI. */
#include "virtio_gpu.h"
#include "display.h"
#include "../core/bus.h"
#include "../core/irq.h"
#include "../port/dma.h"
#include "../virtio/transport.h"
#include "../../arch/arch.h"
#include "../../time/clock.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include "../../../ports/unikraft/split_queue.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
extern void nas_print(const char *);
extern void nas_dec(uint64_t);

#define GPU_ID 16U
#define GPU_WIDTH 800U
#define GPU_HEIGHT 600U
#define GPU_PITCH (GPU_WIDTH * 4U)
#define GPU_BYTES ((uint64_t)GPU_PITCH * GPU_HEIGHT)
#define GPU_PAGES ((GPU_BYTES + NAS_DMA_PAGE_BYTES - 1U) / NAS_DMA_PAGE_BYTES)
#define GPU_ENTRY_BYTES 16U
#define GPU_ENTRY_PAGES ((GPU_PAGES * GPU_ENTRY_BYTES + NAS_DMA_PAGE_BYTES - 1U) / NAS_DMA_PAGE_BYTES)
#define GPU_CONTROL_DESCS 16U
#define GPU_RESOURCE_ID 1U
#define GPU_FORMAT_B8G8R8X8_UNORM 2U
#define GPU_FENCE 1U
#define GPU_CMD_GET_DISPLAY_INFO 0x0100U
#define GPU_CMD_RESOURCE_CREATE_2D 0x0101U
#define GPU_CMD_SET_SCANOUT 0x0103U
#define GPU_CMD_RESOURCE_FLUSH 0x0104U
#define GPU_CMD_TRANSFER_TO_HOST_2D 0x0105U
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106U
#define GPU_RESP_OK_NODATA 0x1100U
#define GPU_RESP_OK_DISPLAY_INFO 0x1101U
#define GPU_TIMEOUT_NS UINT64_C(3000000000)

_Static_assert(GPU_PAGES == 469U, "800x600 DMA scatter page count");
_Static_assert(GPU_ENTRY_PAGES == 2U, "attach command must cross a page");

struct gpu_hdr {
    uint32_t type, flags;
    uint64_t fence_id;
    uint32_t ctx_id, ring_idx;
};
struct gpu_rect {uint32_t x, y, width, height;};
struct gpu_mode {struct gpu_rect r; uint32_t enabled, flags;};
struct gpu_resp_info {struct gpu_hdr hdr; struct gpu_mode modes[16];};
struct gpu_create {struct gpu_hdr hdr; uint32_t id, format, width, height;};
struct gpu_attach {struct gpu_hdr hdr; uint32_t id, count;};
struct gpu_scanout {struct gpu_hdr hdr; struct gpu_rect r; uint32_t scanout, id;};
struct gpu_transfer {struct gpu_hdr hdr; struct gpu_rect r; uint64_t offset; uint32_t id, pad;};
struct gpu_flush {struct gpu_hdr hdr; struct gpu_rect r; uint32_t id, pad;};
struct gpu_entry {uint64_t address; uint32_t length, padding;};
_Static_assert(sizeof(struct gpu_hdr)==24U, "virtio-gpu control header");
_Static_assert(sizeof(struct gpu_resp_info)==408U, "virtio-gpu display info");
_Static_assert(sizeof(struct gpu_create)==40U, "virtio-gpu create2d");
_Static_assert(sizeof(struct gpu_attach)==32U, "virtio-gpu attach");
_Static_assert(sizeof(struct gpu_scanout)==48U, "virtio-gpu scanout");
_Static_assert(sizeof(struct gpu_transfer)==56U, "virtio-gpu transfer");
_Static_assert(sizeof(struct gpu_flush)==48U, "virtio-gpu flush");
_Static_assert(sizeof(struct gpu_entry)==GPU_ENTRY_BYTES, "virtio-gpu mem entry");

enum gpu_runtime_phase { GPU_RUNTIME_IDLE, GPU_RUNTIME_TRANSFER,
                         GPU_RUNTIME_FLUSH };

struct gpu_device {
    struct nas_virtio_transport transport;
    struct nas_bus_device *owner;
    struct nas_vring_queue control;
    struct nas_irq_action irq;
    struct nas_dma_page command, response;
    struct nas_dma_page entries[GPU_ENTRY_PAGES];
    struct nas_dma_page pixels[GPU_PAGES];
    struct nas_display_device display;
    uint64_t submitted, completed, transfers, flushes, irq_acks, next_fence;
    uint32_t irq_pending;
    uint32_t scanout;
    unsigned pixel_pages, entry_pages;
    uint32_t dirty_y0, dirty_y1;
    uint32_t epoch_first, epoch_last;
    uint16_t runtime_head;
    uint64_t runtime_fence, runtime_deadline;
    enum gpu_runtime_phase runtime_phase;
    bool dirty, claimed, reported, stalled;
};
static struct gpu_device gpu;

static uint32_t little32(uint32_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return v;
#else
    return __builtin_bswap32(v);
#endif
}
static uint64_t little64(uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return v;
#else
    return __builtin_bswap64(v);
#endif
}
static struct gpu_hdr command_hdr(struct gpu_device *g,uint32_t type) {
    return (struct gpu_hdr){.type=little32(type),.flags=little32(GPU_FENCE),
                            .fence_id=little64(++g->next_fence)};
}

static void irq_notify(void *context,uint32_t asserted) {
    struct gpu_device *g=context;
    if(g && (asserted&(NAS_VIRTIO_IRQ_USED_BUFFER|
                        NAS_VIRTIO_IRQ_CONFIG_CHANGE))) {
        __atomic_fetch_add(&g->irq_acks,1U,__ATOMIC_RELAXED);
        __atomic_fetch_or(&g->irq_pending,asserted,__ATOMIC_RELEASE);
    }
    /* No DRM, framebuffer, queue drain, or allocation in hardirq context. */
}

/* Bootstrap and runtime share one DMA submission path. Runtime completion
 * owns the head/fence in gpu_device, never a borrowed scheduler C stack. */
static int command_submit(struct gpu_device *g,const void *request,
                          uint32_t request_bytes,uint32_t entry_bytes,
                          uint16_t *submitted_head) {
    if(g && g->stalled)return -EIO;
    if(!g || !g->control.active || !request || !submitted_head ||
       request_bytes>NAS_DMA_PAGE_BYTES ||
       entry_bytes>GPU_ENTRY_PAGES*NAS_DMA_PAGE_BYTES)return -EINVAL;
    memset(g->command.cpu,0,NAS_DMA_PAGE_BYTES);
    memset(g->response.cpu,0,NAS_DMA_PAGE_BYTES);
    memcpy(g->command.cpu,request,request_bytes);
    int rc=nas_dma_sync(g->transport.dma,&g->command,0,request_bytes,
                        NAS_DMA_TO_DEVICE);
    if(rc)return rc;
    struct nas_vring_segment segments[GPU_ENTRY_PAGES+2U];
    unsigned n=0;
    segments[n++]=(struct nas_vring_segment){g->command.bus_address,
                                            request_bytes,false};
    for(unsigned i=0;entry_bytes && i<GPU_ENTRY_PAGES;i++) {
        const uint32_t bytes=entry_bytes>NAS_DMA_PAGE_BYTES ?
                             NAS_DMA_PAGE_BYTES:entry_bytes;
        if(!g->entries[i].cpu)return -EIO;
        rc=nas_dma_sync(g->transport.dma,&g->entries[i],0,bytes,
                        NAS_DMA_TO_DEVICE);
        if(rc)return rc;
        segments[n++]=(struct nas_vring_segment){g->entries[i].bus_address,
                                                bytes,false};
        entry_bytes-=bytes;
    }
    segments[n++]=(struct nas_vring_segment){g->response.bus_address,
                              sizeof(struct gpu_resp_info),true};
    rc=nas_dma_sync(g->transport.dma,&g->response,0,
                    sizeof(struct gpu_resp_info),NAS_DMA_FROM_DEVICE);
    if(rc)return rc;
    rc=nas_vring_queue_submit(&g->control,segments,n,submitted_head);
    if(rc)return rc;
    ++g->submitted;
    return 0;
}

static int command_response(struct gpu_device *g,const struct gpu_hdr *question,
                            uint32_t expected_response,uint32_t response_bytes) {
    if(response_bytes<sizeof(struct gpu_hdr) ||
       response_bytes>sizeof(struct gpu_resp_info))return -EIO;
    int rc=nas_dma_sync(g->transport.dma,&g->response,0,response_bytes,
                        NAS_DMA_FROM_DEVICE);
    if(rc)return rc;
    const struct gpu_hdr *answer=(const struct gpu_hdr *)g->response.cpu;
    if(little32(answer->type)!=expected_response ||
       !(little32(answer->flags)&GPU_FENCE) ||
       answer->fence_id!=question->fence_id)return -EIO;
    if(expected_response==GPU_RESP_OK_DISPLAY_INFO &&
       response_bytes<sizeof(struct gpu_resp_info))return -EIO;
    ++g->completed;
    return 0;
}

/* Only the four pre-userspace initialization commands may busy-wait: no
 * native Task or kernel worker is yet able to consume device completion. */
static int command_sync(struct gpu_device *g,const void *request,
                        uint32_t request_bytes,uint32_t entry_bytes,
                        uint32_t expected_response) {
    uint16_t submitted_head=0;
    int rc=command_submit(g,request,request_bytes,entry_bytes,&submitted_head);
    if(rc)return rc;
    const uint64_t start=nas_arch_monotonic_ns();
    uint16_t completed_head=0;
    uint32_t response_bytes=0;
    do {
        rc=nas_vring_queue_poll(&g->control,&completed_head,&response_bytes);
        if(rc!=-EAGAIN)break;
        /* Bootstrap runs before the scheduler can park any C frame. */
        __asm__ volatile("" : : : "memory");
    }while(nas_arch_monotonic_ns()-start<GPU_TIMEOUT_NS);
    if(rc==-EAGAIN)rc=-ETIMEDOUT;
    if(rc)goto poisoned;
    if(completed_head!=submitted_head) {rc=-EIO;goto poisoned;}
    rc=command_response(g,request,expected_response,response_bytes);
    if(rc)goto poisoned;
    return 0;
poisoned:
    /* A timed-out/invalid used entry may still reference these DMA pages.
     * Never reuse the shared command and reply buffers after uncertain
     * completion. Keep their lifetime pinned until transport reset/unbind. */
    g->stalled=true;
    return rc;
}

static void release_dma(struct gpu_device *g) {
    /* IRQ is detached before transport reset, reset before freeing ANY page
     * that a late host DMA request could still reference. */
    (void)nas_irq_unbind(&g->irq);
    if(g->control.active) {
        (void)nas_vring_queue_close(&g->control);
    }else if(g->transport.ops && g->transport.ops->reset) {
        g->transport.ops->reset(g->transport.context);
    }
    for(unsigned i=0;i<g->pixel_pages;i++)
        if(g->pixels[i].cpu)
            (void)nas_dma_free_page(g->transport.dma,&g->pixels[i]);
    for(unsigned i=0;i<g->entry_pages;i++)
        if(g->entries[i].cpu)
            (void)nas_dma_free_page(g->transport.dma,&g->entries[i]);
    if(g->command.cpu)(void)nas_dma_free_page(g->transport.dma,&g->command);
    if(g->response.cpu)(void)nas_dma_free_page(g->transport.dma,&g->response);
    memset(g,0,sizeof(*g));
}

static void mark_dirty(struct gpu_device *g,uint32_t first,uint32_t last) {
    if(!g->dirty) {g->dirty_y0=first;g->dirty_y1=last;g->dirty=true;}
    else {
        if(first<g->dirty_y0)g->dirty_y0=first;
        if(last>g->dirty_y1)g->dirty_y1=last;
    }
}
static uint64_t read_at(struct nas_display_device *display,uint64_t offset,
                        uint8_t *dst,uint64_t bytes) {
    struct gpu_device *g=display->backend;
    if(!g || offset>GPU_BYTES || bytes>GPU_BYTES-offset)return 0;
    uint64_t done=0;
    while(done<bytes) {
        const uint64_t pos=offset+done;
        const unsigned page=(unsigned)(pos/NAS_DMA_PAGE_BYTES);
        const uint32_t at=(uint32_t)(pos%NAS_DMA_PAGE_BYTES);
        uint64_t part=NAS_DMA_PAGE_BYTES-at;
        if(part>bytes-done)part=bytes-done;
        memcpy(dst+done,(const uint8_t *)g->pixels[page].cpu+at,(size_t)part);
        done+=part;
    }
    return done;
}
static uint64_t write_at(struct nas_display_device *display,uint64_t offset,
                         const uint8_t *src,uint64_t bytes) {
    struct gpu_device *g=display->backend;
    if(!g || offset>GPU_BYTES || bytes>GPU_BYTES-offset)return 0;
    uint64_t done=0;
    while(done<bytes) {
        const uint64_t pos=offset+done;
        const unsigned page=(unsigned)(pos/NAS_DMA_PAGE_BYTES);
        const uint32_t at=(uint32_t)(pos%NAS_DMA_PAGE_BYTES);
        uint64_t part=NAS_DMA_PAGE_BYTES-at;
        if(part>bytes-done)part=bytes-done;
        memcpy((uint8_t *)g->pixels[page].cpu+at,src+done,(size_t)part);
        done+=part;
    }
    if(done)mark_dirty(g,(uint32_t)(offset/GPU_PITCH),
                       (uint32_t)((offset+done-1U)/GPU_PITCH+1U));
    return done;
}

/* Runtime used-ring processing is strictly IRQ driven. An acknowledged
 * interrupt permits one bounded drain, but no scheduler-stack busy wait. */
static int runtime_submit(struct gpu_device *g,const struct gpu_hdr *request,
                          uint32_t bytes,enum gpu_runtime_phase phase) {
    uint16_t head=0;
    int rc=command_submit(g,request,bytes,0,&head);
    if(rc)return rc;
    g->runtime_head=head;
    g->runtime_fence=request->fence_id;
    g->runtime_deadline=nas_time_add_deadline(nas_arch_monotonic_ns(),
                                             GPU_TIMEOUT_NS);
    g->runtime_phase=phase;
    return 0;
}
static int runtime_poison(struct gpu_device *g,int rc) {
    /* Host DMA may still own the command, reply and pixel backing. Never
     * reuse or release them before transport reset, even after timeout. */
    g->stalled=true;
    mark_dirty(g,g->epoch_first,g->epoch_last);
    return rc;
}
static int runtime_complete(struct gpu_device *g) {
    if(g->runtime_phase==GPU_RUNTIME_IDLE)return 0;
    const uint32_t irq=__atomic_exchange_n(&g->irq_pending,0U,__ATOMIC_ACQUIRE);
    if(!(irq&NAS_VIRTIO_IRQ_USED_BUFFER)) {
        if(nas_arch_monotonic_ns()>=g->runtime_deadline)
            return runtime_poison(g,-ETIMEDOUT);
        return 0;
    }
    uint16_t head=0;
    uint32_t bytes=0;
    int rc=nas_vring_queue_poll(&g->control,&head,&bytes);
    if(rc==-EAGAIN)return 0; /* No speculative retry without another IRQ. */
    if(rc || head!=g->runtime_head)
        return runtime_poison(g,rc ? rc : -EIO);
    const struct gpu_hdr expected={.fence_id=g->runtime_fence};
    rc=command_response(g,&expected,GPU_RESP_OK_NODATA,bytes);
    if(rc)return runtime_poison(g,rc);
    if(g->runtime_phase==GPU_RUNTIME_TRANSFER) {
        ++g->transfers;
        struct gpu_rect rect={0,little32(g->epoch_first),little32(GPU_WIDTH),
                              little32(g->epoch_last-g->epoch_first)};
        struct gpu_flush flush={.hdr=command_hdr(g,GPU_CMD_RESOURCE_FLUSH),
                                .r=rect,.id=little32(GPU_RESOURCE_ID)};
        rc=runtime_submit(g,&flush.hdr,sizeof(flush),GPU_RUNTIME_FLUSH);
        return rc ? runtime_poison(g,rc) : 0;
    }
    ++g->flushes;
    g->runtime_phase=GPU_RUNTIME_IDLE;
    return 0;
}

static int gpu_present(struct nas_display_device *display) {
    struct gpu_device *g=display->backend;
    if(!g || !g->claimed)return -ENODEV;
    if(g->stalled)return -EIO;
    int rc=runtime_complete(g);
    if(rc || g->runtime_phase!=GPU_RUNTIME_IDLE)return rc;
    if(!g->dirty)return 0;
    const uint32_t first=g->dirty_y0,last=g->dirty_y1;
    if(last>GPU_HEIGHT || last<=first)return -EINVAL;
    for(unsigned i=first*GPU_PITCH/NAS_DMA_PAGE_BYTES;
        i<((uint64_t)last*GPU_PITCH+NAS_DMA_PAGE_BYTES-1U)/NAS_DMA_PAGE_BYTES;
        ++i) {
        rc=nas_dma_sync(g->transport.dma,&g->pixels[i],0,
                        NAS_DMA_PAGE_BYTES,NAS_DMA_TO_DEVICE);
        if(rc)return rc;
    }
    /* New user writes during an in-flight epoch remain dirty for the next
     * transfer; an older flush must never erase those later writes. */
    g->epoch_first=first;g->epoch_last=last;
    g->dirty=false;
    struct gpu_rect rect={0,little32(first),little32(GPU_WIDTH),
                          little32(last-first)};
    struct gpu_transfer transfer={.hdr=command_hdr(g,GPU_CMD_TRANSFER_TO_HOST_2D),
        .r=rect,.offset=little64((uint64_t)first*GPU_PITCH),
        .id=little32(GPU_RESOURCE_ID)};
    rc=runtime_submit(g,&transfer.hdr,sizeof(transfer),GPU_RUNTIME_TRANSFER);
    return rc ? runtime_poison(g,rc) : 0;
}

static const struct nas_display_ops gpu_display_ops={
    .read_at=read_at,.write_at=write_at,.present=gpu_present
};

static int setup_resource(struct gpu_device *g) {
    uint64_t features=0;
    int rc=nas_virtio_negotiate(&g->transport,0,0,&features);
    if(rc)return rc;
    if(!nas_virtio_config32(&g->transport,8U))return -ENODEV;
    for(unsigned i=0;i<GPU_PAGES;i++) {
        rc=nas_dma_alloc_page(g->transport.dma,&g->pixels[i]);
        if(rc)return rc;
        ++g->pixel_pages;
        memset(g->pixels[i].cpu,0,NAS_DMA_PAGE_BYTES);
    }
    for(unsigned i=0;i<GPU_ENTRY_PAGES;i++) {
        rc=nas_dma_alloc_page(g->transport.dma,&g->entries[i]);
        if(rc)return rc;
        ++g->entry_pages;
        memset(g->entries[i].cpu,0,NAS_DMA_PAGE_BYTES);
    }
    rc=nas_dma_alloc_page(g->transport.dma,&g->command);
    if(rc)return rc;
    rc=nas_dma_alloc_page(g->transport.dma,&g->response);
    if(rc)return rc;
    rc=nas_vring_queue_init(&g->control,&g->transport,0U,GPU_CONTROL_DESCS);
    if(rc)return rc;
    /* Bind before DRIVER_OK and before the first command is published.
     * PCI ISR read-to-clear belongs to this class, never a second platform
     * ACK-only action. Clear any old status before GIC edge unmask. */
    if(g->owner->resource.irq.domain==NAS_IRQ_DOMAIN_NONE ||
       !g->transport.ops || !g->transport.ops->read_ack_irq)return -EOPNOTSUPP;
    (void)nas_virtio_irq_ack(&g->transport);
    rc=nas_irq_bind(&g->irq,g->owner->resource.irq,
                    nas_virtio_irq_ack_source,&g->transport,irq_notify,g);
    if(rc)return rc;
    rc=nas_virtio_activate(&g->transport);
    if(rc)return rc;

    struct gpu_hdr display_info=command_hdr(g,GPU_CMD_GET_DISPLAY_INFO);
    rc=command_sync(g,&display_info,sizeof(display_info),0,
                    GPU_RESP_OK_DISPLAY_INFO);
    if(rc)return rc;
    const struct gpu_resp_info *answer=(const struct gpu_resp_info *)g->response.cpu;
    unsigned chosen=16U;
    for(unsigned i=0;i<16U;i++) {
        const struct gpu_mode *mode=&answer->modes[i];
        if(little32(mode->enabled) &&
           little32(mode->r.width)>=GPU_WIDTH &&
           little32(mode->r.height)>=GPU_HEIGHT) {chosen=i;break;}
    }
    if(chosen==16U)return -EOPNOTSUPP;
    g->scanout=chosen;
    struct gpu_create create={.hdr=command_hdr(g,GPU_CMD_RESOURCE_CREATE_2D),
        .id=little32(GPU_RESOURCE_ID),
        .format=little32(GPU_FORMAT_B8G8R8X8_UNORM),
        .width=little32(GPU_WIDTH),.height=little32(GPU_HEIGHT)};
    rc=command_sync(g,&create,sizeof(create),0,GPU_RESP_OK_NODATA);
    if(rc)return rc;

    for(unsigned i=0;i<GPU_PAGES;i++) {
        const unsigned per_page=NAS_DMA_PAGE_BYTES/GPU_ENTRY_BYTES;
        struct gpu_entry *list=(struct gpu_entry *)g->entries[i/per_page].cpu;
        struct gpu_entry *e=&list[i%per_page];
        const uint32_t length=(i+1U==GPU_PAGES) ?
            (uint32_t)(GPU_BYTES-(uint64_t)i*NAS_DMA_PAGE_BYTES) :
            NAS_DMA_PAGE_BYTES;
        *e=(struct gpu_entry){little64(g->pixels[i].bus_address),
                              little32(length),0};
    }
    struct gpu_attach attach={.hdr=command_hdr(g,GPU_CMD_RESOURCE_ATTACH_BACKING),
        .id=little32(GPU_RESOURCE_ID),.count=little32(GPU_PAGES)};
    rc=command_sync(g,&attach,sizeof(attach),GPU_PAGES*GPU_ENTRY_BYTES,
                    GPU_RESP_OK_NODATA);
    if(rc)return rc;
    struct gpu_scanout scanout={.hdr=command_hdr(g,GPU_CMD_SET_SCANOUT),
        .r={0,0,little32(GPU_WIDTH),little32(GPU_HEIGHT)},
        .scanout=little32(g->scanout),.id=little32(GPU_RESOURCE_ID)};
    rc=command_sync(g,&scanout,sizeof(scanout),0,GPU_RESP_OK_NODATA);
    if(rc)return rc;
    g->display=(struct nas_display_device){
        .name="VirtIO GPU 2D",.backend=g,
        .mode={.width=GPU_WIDTH,.height=GPU_HEIGHT,.pitch=GPU_PITCH,
               .bpp=32,.red_size=8,.red_shift=16,
               .green_size=8,.green_shift=8,.blue_size=8,.blue_shift=0},
        .ops=&gpu_display_ops
    };
    /* Existing Limine boot pixels remain an independent fallback backend.
     * Only the explicit one-time pre-user GPU promotion can replace them. */
    if(!nas_display_boot_takeover(&g->display) &&
       !nas_display_register_primary(&g->display))return -EBUSY;
    g->claimed=true;
    mark_dirty(g,0U,GPU_HEIGHT); /* First visible scanout starts fully black. */
    nas_print("[M177 VIRTIO GPU READY] width=");nas_dec(GPU_WIDTH);
    nas_print(" height=");nas_dec(GPU_HEIGHT);
    nas_print(" backing_pages=");nas_dec(g->pixel_pages);nas_print("\n");
    return 0;
}

static bool match(const struct nas_bus_device *dev) {
    return dev && dev->device_id==GPU_ID && dev->virtio;
}
static int probe(struct nas_bus_device *dev) {
    if(!dev || !dev->virtio)return -ENODEV;
    if(gpu.claimed)return -ENODEV; /* One primary, one owned scanout. */
    memset(&gpu,0,sizeof(gpu));
    gpu.owner=dev;
    gpu.transport=*dev->virtio;
    int rc=setup_resource(&gpu);
    if(rc)release_dma(&gpu);
    return rc;
}
static int remove_device(struct nas_bus_device *dev) {
    /* Until DisplayDevice supports unpin/unregister, an open /dev/fb0 may
     * refer to this scanout forever. Fail closed instead of dangling DMA. */
    return gpu.claimed && gpu.owner==dev ? -EBUSY : -ENODEV;
}
static const struct nas_bus_driver gpu_driver={
    .name="virtio-gpu-2d",.bus_mask=NAS_BUS_MASK_ALL,
    .match=match,.probe=probe,.remove=remove_device
};
int nas_virtio_gpu_driver_register(void) {
    return nas_bus_driver_register(&gpu_driver);
}
unsigned nas_virtio_gpu_count(void) {return gpu.claimed?1U:0U;}
void nas_virtio_gpu_get_stats(struct nas_virtio_gpu_stats *out) {
    if(!out)return;
    *out=(struct nas_virtio_gpu_stats){
        .submitted=gpu.submitted,.completed=gpu.completed,
        .transfers=gpu.transfers,.flushes=gpu.flushes,
        .irq_acks=__atomic_load_n(&gpu.irq_acks,__ATOMIC_RELAXED),
        .backing_pages=gpu.pixel_pages,.scanout_id=gpu.scanout,
        .width=gpu.claimed?GPU_WIDTH:0U,
        .height=gpu.claimed?GPU_HEIGHT:0U
    };
}
void nas_virtio_gpu_report(void) {
    if(!gpu.claimed || gpu.reported || gpu.transfers<5U || gpu.flushes<5U ||
       !__atomic_load_n(&gpu.irq_acks,__ATOMIC_RELAXED))return;
    gpu.reported=true;
    nas_print("[M177 VIRTIO GPU 2D STATS] scanout=");nas_dec(gpu.scanout);
    nas_print(" backing_pages=");nas_dec(gpu.pixel_pages);
    nas_print(" submitted=");nas_dec(gpu.submitted);
    nas_print(" completed=");nas_dec(gpu.completed);
    nas_print(" transfers=");nas_dec(gpu.transfers);
    nas_print(" flushes=");nas_dec(gpu.flushes);
    nas_print(" irq_ack=");nas_dec(gpu.irq_acks);
    nas_print("\n");
    nas_print("[M177 VIRTIO GPU 2D PASS]\n");
}
