/* SPDX-License-Identifier: GPL-2.0-only
 * Port of Linux drivers/virtio/virtio_input.c's device/config/event model
 * onto NaSeed's shared VirtIO transport and upstream Unikraft split vrings.
 * Linux upstream: https://github.com/torvalds/linux/blob/master/drivers/virtio/virtio_input.c
 *
 * The Linux driver's layering is preserved: this hardware driver handles
 * VirtIO configuration, DMA queues and raw input events; hardware-independent
 * input core/evdev own capabilities, state, readers and Linux UAPI framing.
 */
#include "virtio_input.h"
#include "core.h"
#include "../core/bus.h"
#include "../virtio/transport.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include "../../../ports/unikraft/split_queue.h"
#include <virtio/virtio_ids.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Linux include/uapi/linux/virtio_input.h configuration wire ABI. */
#define VIRTIO_INPUT_CFG_ID_NAME       0x01U
#define VIRTIO_INPUT_CFG_ID_SERIAL     0x02U
#define VIRTIO_INPUT_CFG_ID_DEVIDS     0x03U
#define VIRTIO_INPUT_CFG_PROP_BITS     0x10U
#define VIRTIO_INPUT_CFG_EV_BITS       0x11U
#define VIRTIO_INPUT_CFG_ABS_INFO      0x12U
#define VIRTIO_INPUT_EVENTQ            0U
#define VIRTIO_INPUT_STATUSQ           1U
#define VIRTINPUT_RING_SIZE           64U
#define VIRTINPUT_STATUS_RING_SIZE     8U
#define VIRTINPUT_MAX_DEVICES          8U
#define VIRTINPUT_MAX_BITMAP_BYTES   128U
#define VIRTINPUT_EVENT_BYTES          8U

#define VINPUT_EV_KEY 0x01U
#define VINPUT_EV_REL 0x02U
#define VINPUT_EV_ABS 0x03U
#define VINPUT_EV_MSC 0x04U
#define VINPUT_EV_SW  0x05U

struct virtinput_slot {
    struct nas_input_device input;
    struct nas_virtio_transport transport;
    struct nas_vring_queue eventq;
    struct nas_vring_queue statusq;
    struct nas_dma_page event_dma;
    const struct nas_bus_device *bus_device;
    uint16_t slot_by_head[VIRTINPUT_RING_SIZE]; /* 0=unused, otherwise DMA slot + 1 */
    uint8_t device_name[80];
    bool registered;
    bool poisoned;
    bool busy;
};

static struct virtinput_slot inputs[VIRTINPUT_MAX_DEVICES];
static unsigned current_count;
static unsigned poll_cursor;

static uint16_t load16(const uint8_t *v) {
    return (uint16_t)((uint16_t)v[0] | (uint16_t)v[1] << 8);
}
static uint32_t load32(const uint8_t *v) {
    return (uint32_t)v[0] | (uint32_t)v[1] << 8 |
           (uint32_t)v[2] << 16 | (uint32_t)v[3] << 24;
}

/* Linux virtinput_cfg_select(): write BOTH selectors, then read the size.
 * No guest-visible data is invented when the device rejects a selector. */
static int select_config(const struct nas_virtio_transport *transport,
                         uint8_t select, uint8_t subsel, uint8_t *size) {
    int rc=nas_virtio_config_write(transport,0,&select,1);
    if (!rc) rc=nas_virtio_config_write(transport,1,&subsel,1);
    if (!rc) rc=nas_virtio_config_read(transport,2,size,1);
    return rc;
}
static int read_config(const struct nas_virtio_transport *transport,
                       uint8_t select, uint8_t subsel,
                       void *buffer, uint8_t capacity, uint8_t *actual) {
    uint8_t n=0;
    int rc=select_config(transport,select,subsel,&n);
    if (rc) return rc;
    if (actual) *actual=n;
    if (!n) return 0;
    if (n>VIRTINPUT_MAX_BITMAP_BYTES || n>capacity) return -EOVERFLOW;
    return nas_virtio_config_read(transport,8,buffer,n);
}
static int read_caps(struct virtinput_slot *v,uint8_t select,uint8_t subsel,
                     uint16_t type) {
    uint8_t bitmap[VIRTINPUT_MAX_BITMAP_BYTES]={0};
    uint8_t n=0;
    int rc=read_config(&v->transport,select,subsel,
                       bitmap,sizeof(bitmap),&n);
    if (rc) return rc;
    for (unsigned i=0;i<(unsigned)n*8U;i++) {
        if (!(bitmap[i/8U] & (1U<<(i%8U)))) continue;
        nas_input_set_capability(&v->input,type,(uint16_t)i);
    }
    return 0;
}
static int read_properties(struct virtinput_slot *v) {
    uint8_t bitmap[VIRTINPUT_MAX_BITMAP_BYTES]={0};
    uint8_t n=0;
    int rc=read_config(&v->transport,VIRTIO_INPUT_CFG_PROP_BITS,0,
                       bitmap,sizeof(bitmap),&n);
    if (rc) return rc;
    for (unsigned i=0;i<(unsigned)n*8U;i++)
        if (bitmap[i/8U] & (1U<<(i%8U)))
            (void)nas_input_set_property(&v->input,(uint16_t)i);
    return 0;
}
static int describe_input(struct virtinput_slot *v) {
    uint8_t n=0;
    uint8_t name_buf[VIRTINPUT_MAX_BITMAP_BYTES]={0};
    int rc=read_config(&v->transport,VIRTIO_INPUT_CFG_ID_NAME,0,
                       name_buf,sizeof(name_buf),&n);
    if (rc) return rc;
    unsigned length=n<sizeof(v->device_name)-1U ? n : sizeof(v->device_name)-1U;
    memcpy(v->device_name,name_buf,length);
    v->device_name[length]=0;
    const char *name=length?(const char *)v->device_name:"VirtIO Input";
    nas_input_device_init(&v->input,name);
    uint8_t devids[8]={0};
    rc=read_config(&v->transport,VIRTIO_INPUT_CFG_ID_DEVIDS,0,
                   devids,sizeof(devids),&n);
    if (rc) return rc;
    if (n==sizeof(devids)) {
        v->input.id.bustype=load16(devids);
        v->input.id.vendor=load16(devids+2);
        v->input.id.product=load16(devids+4);
        v->input.id.version=load16(devids+6);
    } else if(n) return -EIO;
    else v->input.id.bustype=0x06U; /* Linux BUS_VIRTUAL */

    /* The generic input core adds EV_SYN when registering. This device
     * publishes the same type/capability bits as Linux virtio_input.c. */
    rc=read_properties(v);
    if (rc) return rc;
    static const uint8_t inbound[]={
        VINPUT_EV_KEY,VINPUT_EV_REL,VINPUT_EV_ABS,VINPUT_EV_MSC,VINPUT_EV_SW};
    for (unsigned j=0;j<sizeof(inbound);j++) {
        uint8_t type=inbound[j];
        rc=read_caps(v,VIRTIO_INPUT_CFG_EV_BITS,type,type);
        if (rc) return rc;
    }
    /* Linux routes EV_LED/SND through virtinput_status() and statusq. The
     * minimal NaSeed input core does not yet have that output callback:
     * do not advertise writable LED/SND features to userspace until they
     * can actually be serviced. The required statusq is still initialized. */
    /* Absolute axes include min/max and resolution, necessary for mouse
     * pointer mapping and Linux EVIOCGABS queries by real applications. */
    for (unsigned axis=0;axis<64U;axis++) {
        if (!(v->input.absbit[axis/8U] & (1U<<(axis%8U)))) continue;
        uint8_t abs[20]={0};
        rc=read_config(&v->transport,VIRTIO_INPUT_CFG_ABS_INFO,(uint8_t)axis,
                       abs,sizeof(abs),&n);
        if (rc) return rc;
        if (n!=sizeof(abs)) return -EIO;
        nas_input_set_absinfo(&v->input,(uint16_t)axis,
                             (int32_t)load32(abs),
                             (int32_t)load32(abs+4),
                             (int32_t)load32(abs+8),
                             (int32_t)load32(abs+12),
                             (int32_t)load32(abs+16));
    }
    return 0;
}

static void stop_device(struct virtinput_slot *v) {
    /* Queue close performs device reset before freeing DMA. If the two
     * queues are present, only one reset is required, but both queue
     * allocations remain owned until every DMA transfer has stopped. */
    if (v->eventq.active) (void)nas_vring_queue_close(&v->eventq);
    if (v->statusq.active) (void)nas_vring_queue_close(&v->statusq);
    if (v->event_dma.cpu) (void)nas_dma_free_page(v->transport.dma,&v->event_dma);
    if (v->transport.ops && v->transport.ops->reset)
        v->transport.ops->reset(v->transport.context);
}
static void poison(struct virtinput_slot *v) {
    v->poisoned=true;
    nas_virtio_fail(&v->transport);
}
static int queue_slot(struct virtinput_slot *v,unsigned slot) {
    if (slot>=VIRTINPUT_RING_SIZE || v->poisoned) return -EIO;
    struct nas_vring_segment seg={
        v->event_dma.bus_address+slot*VIRTINPUT_EVENT_BYTES,
        VIRTINPUT_EVENT_BYTES,true
    };
    uint16_t head=0;
    int rc=nas_dma_sync(v->transport.dma,&v->event_dma,
                        slot*VIRTINPUT_EVENT_BYTES,VIRTINPUT_EVENT_BYTES,
                        NAS_DMA_FROM_DEVICE);
    if (!rc) rc=nas_vring_queue_submit(&v->eventq,&seg,1,&head);
    if (rc) return rc;
    if (head>=VIRTINPUT_RING_SIZE || v->slot_by_head[head]) return -EIO;
    v->slot_by_head[head]=(uint16_t)(slot+1U);
    return 0;
}
static int probe(struct nas_bus_device *dev) {
    if (!dev || dev->device_id!=VIRTIO_ID_INPUT || !dev->virtio) return -ENODEV;
    unsigned slot=VIRTINPUT_MAX_DEVICES;
    for (unsigned i=0;i<VIRTINPUT_MAX_DEVICES;i++) {
        if (inputs[i].bus_device==dev) return -EEXIST;
        if (!inputs[i].bus_device && slot==VIRTINPUT_MAX_DEVICES) slot=i;
    }
    if (slot==VIRTINPUT_MAX_DEVICES) return -ENOSPC;
    struct virtinput_slot *v=&inputs[slot];
    memset(v,0,sizeof(*v));
    v->transport=*dev->virtio;
    if (!v->transport.dma || !v->transport.dma->coherent) return -EOPNOTSUPP;

    /* Linux virtinput_probe: negotiate, describe device, allocate VQs,
     * register input_dev, publish DMA buffers AFTER DRIVER_OK. */
    uint64_t features=0;
    int rc=nas_virtio_negotiate(&v->transport,0,0,&features);
    if (rc) goto fail;
    rc=describe_input(v);
    if (rc) goto fail;
    rc=nas_dma_alloc_page(v->transport.dma,&v->event_dma);
    if (rc) goto fail;
    rc=nas_vring_queue_init(&v->eventq,&v->transport,VIRTIO_INPUT_EVENTQ,
                            VIRTINPUT_RING_SIZE);
    if (rc) goto fail;
    rc=nas_vring_queue_init(&v->statusq,&v->transport,VIRTIO_INPUT_STATUSQ,
                            VIRTINPUT_STATUS_RING_SIZE);
    if (rc) goto fail;
    rc=nas_virtio_activate(&v->transport);
    if (rc) goto fail;
    rc=nas_input_register_device(&v->input);
    if (rc) goto fail;
    v->registered=true;
    for (unsigned i=0;i<VIRTINPUT_RING_SIZE;i++) {
        rc=queue_slot(v,i);
        if (rc) goto fail;
    }
    v->bus_device=dev;
    ++current_count;
    return 0;
fail:
    if (v->registered) (void)nas_input_unregister_device(&v->input);
    stop_device(v);
    memset(v,0,sizeof(*v));
    return rc;
}
static int remove_device(struct nas_bus_device *dev) {
    for (unsigned i=0;i<VIRTINPUT_MAX_DEVICES;i++) {
        struct virtinput_slot *v=&inputs[i];
        if (v->bus_device!=dev) continue;
        if (v->busy) return -EBUSY;
        int rc=nas_input_unregister_device(&v->input);
        if (rc) return rc;
        stop_device(v);
        memset(v,0,sizeof(*v));
        --current_count;
        return 0;
    }
    return -ENODEV;
}
static bool matches(const struct nas_bus_device *dev) {
    return dev && dev->device_id==VIRTIO_ID_INPUT && dev->virtio;
}
static const struct nas_bus_driver driver={
    .name="linux-virtio-input-port", .bus_mask=NAS_BUS_MASK_ALL,
    .match=matches,.probe=probe,.remove=remove_device
};
int nas_virtio_input_driver_register(void) {
    return nas_bus_driver_register(&driver);
}
unsigned nas_virtio_input_count(void) { return current_count; }

/* Bounded drain of completed device descriptors. Event payload remains
 * unmodified Linux little-endian virtio_input_event (u16,u16,s32).
 * Unlike a fabricated TTY byte, EV_KEY records remain hardware-independent
 * keycodes, and EV_SYN packet boundaries are forwarded to input core. */
int nas_virtio_input_poll(unsigned budget) {
    if (!budget) return 0;
    int done=0;
    const unsigned start=poll_cursor;
    poll_cursor=(poll_cursor+1U)%VIRTINPUT_MAX_DEVICES;
    for (unsigned step=0;step<VIRTINPUT_MAX_DEVICES &&
         (unsigned)done<budget;step++) {
        unsigned i=(start+step)%VIRTINPUT_MAX_DEVICES;
        struct virtinput_slot *v=&inputs[i];
        if (!v->bus_device || v->poisoned ||
            __atomic_exchange_n(&v->busy,true,__ATOMIC_ACQUIRE)) continue;
        /* One flooding keyboard must not starve a pointer sharing the
         * same per-CPU service budget; next service rotates the first
         * device as well. */
        unsigned quota=(budget-(unsigned)done)>8U?8U:budget-(unsigned)done;
        unsigned served=0;
        while (served<quota) {
            uint16_t head=0;
            uint32_t bytes=0;
            int rc=nas_vring_queue_poll(&v->eventq,&head,&bytes);
            if (rc==-EAGAIN) break;
            if (rc || head>=VIRTINPUT_RING_SIZE ||
                !v->slot_by_head[head] || bytes!=VIRTINPUT_EVENT_BYTES) {
                poison(v);
                break;
            }
            unsigned slot=v->slot_by_head[head]-1U;
            v->slot_by_head[head]=0;
            rc=nas_dma_sync(v->transport.dma,&v->event_dma,
                            slot*VIRTINPUT_EVENT_BYTES,VIRTINPUT_EVENT_BYTES,
                            NAS_DMA_FROM_DEVICE);
            if (rc) {poison(v);break;}
            const uint8_t *event=(const uint8_t *)v->event_dma.cpu+
                                  slot*VIRTINPUT_EVENT_BYTES;
            nas_input_event(&v->input,load16(event),load16(event+2),
                            (int32_t)load32(event+4));
            ++done;
            ++served;
            if (queue_slot(v,slot)) {poison(v);break;}
        }
        __atomic_store_n(&v->busy,false,__ATOMIC_RELEASE);
    }
    return done;
}
