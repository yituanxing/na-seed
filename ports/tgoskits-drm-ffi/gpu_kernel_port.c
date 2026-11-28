/* SPDX-License-Identifier: MIT
 * QEMU-only NaSeed providers for the ORIGINAL TGOSKits virtio-gpu 3D core.
 * No hand-written 3D protocol, no second queue owner, no DRM render node.
 * One GPU device is probed exclusively by the imported Rust implementation.
 */
#include "gpu_pci_identity.h"
#include "../../kernel/drivers/core/bus.h"
#include "../../kernel/fs/drm_chrdev.h"
#include "../../kernel/drivers/virtio/transport.h"
#include "../../kernel/drivers/port/dma.h"
#include "../../kernel/mm/frame.h"
#include "../../kernel/errno.h"
#include "../../kernel/lib/string.h"
#include "../../kernel/drivers/serial.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef NAS_EXPERIMENT_TGOS_GPU_OWNER
#error "Original GPU port is permitted only in isolated experimental mode"
#endif

#define TGOS_GPU_ID 16U
#define TGOS_GPU_MAX_PAGES 4096U
#define TGOS_GPU_MAX_BOUNCES 256U
#define TGOS_GPU_PAGE 4096U

/* HAL::share() sees arbitrary TLSF kernel heap allocations. The C kernel
 * image and heap are NOT physically contiguous address ranges, so blindly
 * subtracting the HHDM offset is invalid. Reserve separate contiguous PMM
 * frames for a bounce mapping, kept until HAL::unshare() after completion.
 * This bootstrap bridge is single-threaded and must never run in IRQ.
 */
struct nas_tgos_bounce {
    uint64_t pa;
    void *original;
    uint32_t bytes, pages, direction;
};
static struct nas_tgos_bounce bounce[TGOS_GPU_MAX_BOUNCES];

static bool valid_pages(size_t pages) {
    return pages && pages <= TGOS_GPU_MAX_PAGES &&
           pages <= SIZE_MAX / TGOS_GPU_PAGE;
}
static void *cpu_alias(uint64_t pa) {
    uint64_t offset=nas_hhdm_offset();
    if(!pa || offset>UINTPTR_MAX-pa)return NULL;
    return (void *)(uintptr_t)(offset+pa);
}
static int release_pages(uint64_t pa,size_t pages) {
    if(!pa || !valid_pages(pages) ||
       pages-1U>(UINT64_MAX-pa)/TGOS_GPU_PAGE)return -EINVAL;
    int rc=0;
    for(size_t i=0;i<pages;i++)
        if(!nas_frame_put(pa+i*TGOS_GPU_PAGE))rc=-EIO;
    return rc;
}
int nas_tgos_gpu_dma_alloc(size_t pages,uint64_t *out_pa,void **out_va) {
    if(!out_pa || !out_va || !valid_pages(pages))return -EINVAL;
    *out_pa=0;*out_va=NULL;
    uint64_t pa=nas_frame_alloc_contiguous(pages);
    if(!pa)return -ENOMEM;
    void *alias=cpu_alias(pa);
    if(!alias) {
        (void)release_pages(pa,pages);
        return -EOVERFLOW;
    }
    *out_pa=pa;*out_va=alias;
    return 0;
}
int nas_tgos_gpu_dma_free(uint64_t pa,void *va,size_t pages) {
    if(!valid_pages(pages) || !va || cpu_alias(pa)!=va)return -EINVAL;
    return release_pages(pa,pages);
}
uint64_t nas_tgos_gpu_bounce_share(void *original,size_t bytes,uint32_t direction) {
    if(!original || !bytes || bytes>UINT32_MAX ||
       (direction!=1U && direction!=2U && direction!=3U) ||
       bytes>SIZE_MAX-(TGOS_GPU_PAGE-1U))return 0;
    size_t pages=(bytes+TGOS_GPU_PAGE-1U)/TGOS_GPU_PAGE;
    if(!valid_pages(pages))return 0;
    unsigned index=TGOS_GPU_MAX_BOUNCES;
    for(unsigned i=0;i<TGOS_GPU_MAX_BOUNCES;i++)
        if(!bounce[i].pa){index=i;break;}
    if(index==TGOS_GPU_MAX_BOUNCES)return 0;
    uint64_t pa=nas_frame_alloc_contiguous(pages);
    if(!pa)return 0;
    void *alias=cpu_alias(pa);
    if(!alias) {
        (void)release_pages(pa,pages);
        return 0;
    }
    if(direction!=2U)memcpy(alias,original,bytes);
    bounce[index]=(struct nas_tgos_bounce){
        .pa=pa,.original=original,.bytes=(uint32_t)bytes,
        .pages=(uint32_t)pages,.direction=direction
    };
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return pa; /* QEMU identity-DMA only; NOT an IOMMU physical address API */
}
int nas_tgos_gpu_bounce_unshare(uint64_t pa,void *original,size_t bytes,
                                 uint32_t direction) {
    for(unsigned i=0;i<TGOS_GPU_MAX_BOUNCES;i++) {
        struct nas_tgos_bounce *b=&bounce[i];
        if(b->pa!=pa || !pa)continue;
        if(b->original!=original || bytes!=b->bytes || direction!=b->direction)
            return -EINVAL;
        void *alias=cpu_alias(pa);
        if(!alias)return -EIO;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if(direction!=1U)memcpy(original,alias,bytes);
        int rc=release_pages(pa,b->pages);
        if(rc)return rc; /* retain the mapping, do not pretend success */
        *b=(struct nas_tgos_bounce){0};
        return 0;
    }
    return -EINVAL;
}
void nas_tgos_gpu_bounce_reap_after_reset(void) {
    /* Caller MUST have already reset the entire device. A GPU that can still
     * DMA could otherwise write to free frames. Never copy stale replies. */
    for(unsigned i=0;i<TGOS_GPU_MAX_BOUNCES;i++) {
        struct nas_tgos_bounce *b=&bounce[i];
        if(!b->pa)continue;
        if(!release_pages(b->pa,b->pages))
            *b=(struct nas_tgos_bounce){0};
    }
}

/* NaSeed PCI/MMIO transport is borrowed by the GPU owner. The Rust driver
 * chooses ring layout and owns descriptor memory. The C port only forwards
 * accesses to the existing, tested transport ops, never owns a second ring.
 */
static const struct nas_virtio_transport *vt(const void *opaque) {
    return (const struct nas_virtio_transport *)opaque;
}
uint32_t nas_tgos_gpu_vt_status(const void *opaque) {
    const struct nas_virtio_transport *t=vt(opaque);
    return t && t->ops && t->ops->read_status ?
        t->ops->read_status(t->context):0;
}
void nas_tgos_gpu_vt_set_status(const void *opaque,uint32_t value) {
    const struct nas_virtio_transport *t=vt(opaque);
    if(!t || !t->ops)return;
    if(!value) {
        if(t->ops->reset)t->ops->reset(t->context);
    } else if(t->ops->write_status) {
        t->ops->write_status(t->context,value);
    }
}
uint64_t nas_tgos_gpu_vt_features(const void *opaque) {
    const struct nas_virtio_transport *t=vt(opaque);
    if(!t || !t->ops || !t->ops->read_features)return 0;
    return (uint64_t)t->ops->read_features(t->context,0U) |
           ((uint64_t)t->ops->read_features(t->context,1U)<<32U);
}
void nas_tgos_gpu_vt_set_features(const void *opaque,uint64_t features) {
    const struct nas_virtio_transport *t=vt(opaque);
    if(!t || !t->ops || !t->ops->write_features)return;
    t->ops->write_features(t->context,0U,(uint32_t)features);
    t->ops->write_features(t->context,1U,(uint32_t)(features>>32));
}
int nas_tgos_gpu_vt_setup_queue(const void *opaque,uint32_t q,uint32_t size,
                          uint64_t desc,uint64_t driver,uint64_t device) {
    return nas_virtio_setup_queue(vt(opaque),q,size,desc,driver,device);
}
void nas_tgos_gpu_vt_notify(const void *opaque,uint32_t q) {
    nas_virtio_notify(vt(opaque),q);
}
int nas_tgos_gpu_vt_read(const void *opaque,uint32_t off,void *dst,uint32_t len) {
    return nas_virtio_config_read(vt(opaque),off,dst,len);
}
int nas_tgos_gpu_vt_write(const void *opaque,uint32_t off,const void *src,
                           uint32_t len) {
    return nas_virtio_config_write(vt(opaque),off,src,len);
}
uint32_t nas_tgos_gpu_vt_irq_ack(const void *opaque) {
    return nas_virtio_irq_ack(vt(opaque));
}

extern int nas_tgos_original_gpu_probe(const void *,uint32_t *);
extern int nas_tgos_original_gpu_shutdown(void);
static struct nas_bus_device *gpu_owner;
static bool gpu_claimed;
unsigned nas_tgos_original_gpu_active(void) { return gpu_claimed?1U:0U; }
/* Real device ownership and PCI config are borrowed from the common bus.
 * Never guess QEMU BDF or invent vendor/device IDs to satisfy Mesa.
 * A pending unbind keeps its owner pinned until all render Files retire. */
int nas_tgos_original_gpu_pci_identity(struct nas_tgos_gpu_pci_identity *out) {
    if(!out)return -EINVAL;
    *out=(struct nas_tgos_gpu_pci_identity){0};
    if(!gpu_claimed || !gpu_owner || gpu_owner->bus!=NAS_BUS_PCI ||
       gpu_owner->resource.kind!=NAS_BUS_RESOURCE_PCI)return -ENODEV;
    const struct nas_bus_resource *r=&gpu_owner->resource;
    const uint8_t *c=r->view.pci.config;
    if(!c || r->view.pci.config_bytes<48U)return -ENODEV;
    *out=(struct nas_tgos_gpu_pci_identity){
        .domain=0,.bus=r->view.pci.bus,.slot=r->view.pci.slot,
        .function=r->view.pci.function,
        .vendor=(uint32_t)c[0] | ((uint32_t)c[1]<<8U),
        .device=(uint32_t)c[2] | ((uint32_t)c[3]<<8U),
        .revision=c[8],
        .subvendor=(uint32_t)c[44] | ((uint32_t)c[45]<<8U),
        .subdevice=(uint32_t)c[46] | ((uint32_t)c[47]<<8U)
    };
    return 0;
}


static bool match(const struct nas_bus_device *dev) {
    return dev && dev->device_id==TGOS_GPU_ID && dev->virtio;
}
static int probe(struct nas_bus_device *dev) {
    if(!dev || !dev->virtio)return -ENODEV;
    if(gpu_owner)return -EBUSY;
    if(dev->virtio->dma!=&nas_dma_qemu_coherent ||
       !dev->virtio->dma->coherent)
        return -EOPNOTSUPP; /* no IOMMU, noncoherent or hardware DMA port */
    uint32_t capset_id=0;
    int rc=nas_tgos_original_gpu_probe(dev->virtio,&capset_id);
    if(rc) {
        nas_print("[TGOS ORIGINAL GPU OWNER FAIL] error=");
        nas_dec((uint64_t)(-((int64_t)rc)));nas_print("\n");
        return rc;
    }
    gpu_owner=dev;
    gpu_claimed=true;
    nas_print("[TGOS ORIGINAL GPU 3D RESOURCE FENCE PASS] capset_id=");
    nas_dec(capset_id);
    nas_print(" original-res3d-submit3d-fence=1 render-node=caps-only\n");
    return 0;
}
static int remove_gpu(struct nas_bus_device *dev) {
    if(!gpu_claimed || gpu_owner!=dev)return -ENODEV;
    if(nas_drm_tgos_render_busy())return -EBUSY;
    int rc=nas_tgos_original_gpu_shutdown();
    if(rc)return rc;
    gpu_owner=NULL;gpu_claimed=false;
    return 0;
}
static const struct nas_bus_driver tgos_driver={
    .name="tgos-original-virtio-gpu",
    .bus_mask=NAS_BUS_MASK_ALL,
    .match=match,.probe=probe,.remove=remove_gpu
};
int nas_tgos_original_gpu_driver_register(void) {
    return nas_bus_driver_register(&tgos_driver);
}
