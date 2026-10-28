#include "pci.h"
#include "../../errno.h"
#include <stdint.h>
#include <stddef.h>
#define CAP_COMMON 1U
#define CAP_NOTIFY 2U
#define CAP_DEVICE 4U
#define CFG_STATUS 0x06U
#define CFG_CAP_PTR 0x34U
#define PCI_CAP_VENDOR 0x09U
#define COMMON_DF_SELECT 0x00U
#define COMMON_DF 0x04U
#define COMMON_GF_SELECT 0x08U
#define COMMON_GF 0x0cU
#define COMMON_NQUEUES 0x12U
#define COMMON_STATUS 0x14U
#define COMMON_QSELECT 0x16U
#define COMMON_QSIZE 0x18U
#define COMMON_QENABLE 0x1cU
#define COMMON_QNOTIFY 0x1eU
#define COMMON_QDESC 0x20U
#define COMMON_QDRIVER 0x28U
#define COMMON_QDEVICE 0x30U
#define COMMON_MIN_BYTES 0x38U
static uint16_t cfg16(const uint8_t *p,unsigned n) {
    return (uint16_t)(p[n] | ((uint16_t)p[n+1] << 8));
}
static uint32_t cfg32(const uint8_t *p,unsigned n) {
    return (uint32_t)p[n] | ((uint32_t)p[n+1] << 8) |
           ((uint32_t)p[n+2] << 16) | ((uint32_t)p[n+3] << 24);
}
static uint16_t r16(volatile uint8_t *b,unsigned o) {
    return *(volatile uint16_t *)(void *)(b+o);
}
static uint32_t r32(volatile uint8_t *b,unsigned o) {
    return *(volatile uint32_t *)(void *)(b+o);
}
static void w8(volatile uint8_t *b,unsigned o,uint8_t v) { b[o]=v; }
static void w16(volatile uint8_t *b,unsigned o,uint16_t v) {
    *(volatile uint16_t *)(void *)(b+o)=v;
}
static void w32(volatile uint8_t *b,unsigned o,uint32_t v) {
    *(volatile uint32_t *)(void *)(b+o)=v;
}
static void w64(volatile uint8_t *b,unsigned o,uint64_t v) {
    /* Use halves to avoid 64-bit MMIO accesses on future 32-bit hosts. */
    w32(b,o,(uint32_t)v);w32(b,o+4,(uint32_t)(v>>32));
}
static void pci_reset(void *context) {
    struct nas_virtio_pci *p=context;
    w8(p->common,COMMON_STATUS,0);
    for(unsigned i=0;i<NAS_VIRTIO_PCI_MAX_QUEUES;i++)
        p->queue_notify[i]=NULL;
}
static uint32_t pci_read_status(void *context) {
    struct nas_virtio_pci *p=context;
    return p->common[COMMON_STATUS];
}
static void pci_write_status(void *context,uint32_t value) {
    struct nas_virtio_pci *p=context;
    w8(p->common,COMMON_STATUS,(uint8_t)value);
}
static uint32_t pci_read_features(void *context,unsigned bank) {
    struct nas_virtio_pci *p=context;
    w32(p->common,COMMON_DF_SELECT,bank);
    return r32(p->common,COMMON_DF);
}
static void pci_write_features(void *context,unsigned bank,uint32_t value) {
    struct nas_virtio_pci *p=context;
    w32(p->common,COMMON_GF_SELECT,bank);
    w32(p->common,COMMON_GF,value);
}
static int pci_setup_queue(void *context,unsigned qid,unsigned count,
                           uint64_t desc,uint64_t driver,uint64_t device) {
    struct nas_virtio_pci *p=context;
    if(qid>=NAS_VIRTIO_PCI_MAX_QUEUES || qid>=r16(p->common,COMMON_NQUEUES) ||
       !count || count>UINT16_MAX || (count&(count-1U)))
        return -EINVAL;
    w16(p->common,COMMON_QSELECT,(uint16_t)qid);
    if(r16(p->common,COMMON_QENABLE) || count>r16(p->common,COMMON_QSIZE))
        return -EOPNOTSUPP;
    uint64_t offset=(uint64_t)r16(p->common,COMMON_QNOTIFY)*p->notify_multiplier;
    if(offset>p->notify_size ||
       (uint64_t)p->notify_size-offset<sizeof(uint16_t) ||
       ((uintptr_t)(p->notify+offset)&1U))
        return -EOPNOTSUPP;
    w16(p->common,COMMON_QSIZE,(uint16_t)count);
    w64(p->common,COMMON_QDESC,desc);
    w64(p->common,COMMON_QDRIVER,driver);
    w64(p->common,COMMON_QDEVICE,device);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    w16(p->common,COMMON_QENABLE,1U);
    p->queue_notify[qid]=(volatile uint16_t *)(void *)(p->notify+offset);
    return 0;
}
static void pci_notify(void *context,unsigned qid) {
    struct nas_virtio_pci *p=context;
    if(qid>=NAS_VIRTIO_PCI_MAX_QUEUES || !p->queue_notify[qid]) {
        w8(p->common,COMMON_STATUS,(uint8_t)(p->common[COMMON_STATUS]|128U));
        return;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *p->queue_notify[qid]=(uint16_t)qid;
}
static uint32_t pci_config32(void *context,uint32_t off) {
    struct nas_virtio_pci *p=context;
    if((off&3U) || off>p->device_size ||
       p->device_size-off<sizeof(uint32_t))return 0;
    return r32(p->device,off);
}
static const struct nas_virtio_transport_ops pci_ops={
    .reset=pci_reset,.read_status=pci_read_status,
    .write_status=pci_write_status,.read_features=pci_read_features,
    .write_features=pci_write_features,.setup_queue=pci_setup_queue,
    .notify_queue=pci_notify,.read_config32=pci_config32
};
int nas_virtio_pci_transport_init(struct nas_virtio_transport *out,
                                  struct nas_virtio_pci *pci,
                                  const struct nas_virtio_pci_resources *res,
                                  const struct nas_dma_provider *dma) {
    if(!out || !pci || !res || !res->config || !dma)return -EINVAL;
    const uint8_t *cfg=res->config;
    if(cfg16(cfg,0)!=0x1af4U || cfg16(cfg,2)<0x1000U ||
       cfg16(cfg,2)>0x107fU || !(cfg16(cfg,CFG_STATUS)&0x10U))
        return -ENODEV;
    struct nas_virtio_pci parsed={0};
    unsigned seen=0,traversed=0;
    uint8_t cap=cfg[CFG_CAP_PTR];
    uint8_t visited[256]={0};
    while(cap) {
        if(cap<0x40U || cap>0xfcU || (cap&3U) ||
           ++traversed>48U || visited[cap])return -EINVAL;
        visited[cap]=1U;
        unsigned next=cfg[cap+1];
        /* Every standard PCI capability has only the 2-byte id/next
         * prefix. Byte +2 is a LENGTH solely for vendor-specific cap 09;
         * for MSI-X it is Message Control and may legitimately be zero.
         * Rejecting a zero there broke real QEMU q35 PCI discovery. */
        if(cfg[cap]==PCI_CAP_VENDOR) {
            unsigned cap_len=cfg[cap+2];
            if(cap_len<16U || cap_len>256U-cap)return -EINVAL;
            unsigned type=cfg[cap+3],bar=cfg[cap+4];
            if(type==CAP_COMMON || type==CAP_NOTIFY || type==CAP_DEVICE) {
                unsigned bit=1U<<type;
                if((seen&bit) || bar>=NAS_VIRTIO_PCI_BAR_COUNT ||
                   !res->bars[bar].cpu)return -EINVAL;
                uint64_t offset=cfg32(cfg,cap+8);
                uint64_t length=cfg32(cfg,cap+12);
                uint64_t minimum=type==CAP_COMMON ? COMMON_MIN_BYTES :
                                 type==CAP_NOTIFY ? 2U : 4U;
                if(length<minimum || offset>res->bars[bar].size ||
                   length>res->bars[bar].size-offset ||
                   (uintptr_t)res->bars[bar].cpu>UINTPTR_MAX-offset)
                    return -EINVAL;
                volatile uint8_t *address=res->bars[bar].cpu+(size_t)offset;
                unsigned align=type==CAP_NOTIFY?2U:4U;
                if(((uintptr_t)address&(align-1U))!=0U)return -EINVAL;
                if(type==CAP_COMMON) {
                    if(((uintptr_t)address&7U)!=0U)return -EINVAL;
                    parsed.common=address;
                } else if(type==CAP_DEVICE) {
                    parsed.device=address;parsed.device_size=(uint32_t)length;
                } else {
                    if(cap_len<20U)return -EINVAL;
                    parsed.notify=address;
                    parsed.notify_size=(uint32_t)length;
                    parsed.notify_multiplier=cfg32(cfg,cap+16);
                }
                seen|=bit;
            }
        }
        cap=(uint8_t)next;
    }
    if(seen!=((1U<<CAP_COMMON)|(1U<<CAP_NOTIFY)|(1U<<CAP_DEVICE)))
        return -ENODEV;
    *pci=parsed;
    *out=(struct nas_virtio_transport){.context=pci,.ops=&pci_ops,.dma=dma};
    return 0;
}
