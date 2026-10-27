/* Host exercises real PCI transport operations with mapped BAR memory.
 * It does NOT claim PCI enumeration or x86 QEMU disk persistence. */
#include "../kernel/drivers/virtio/pci.h"
#include "../kernel/errno.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdalign.h>
static void p16(uint8_t *p,unsigned off,uint16_t value){
    p[off]=(uint8_t)value;p[off+1]=(uint8_t)(value>>8);
}
static void p32(uint8_t *p,unsigned off,uint32_t value){
    for(unsigned i=0;i<4;i++)p[off+i]=(uint8_t)(value>>(8*i));
}
static uint16_t g16(const uint8_t *p,unsigned off){
    return (uint16_t)(p[off]|(uint16_t)p[off+1]<<8);
}
static uint32_t g32(const uint8_t *p,unsigned off){
    return (uint32_t)p[off]|(uint32_t)p[off+1]<<8|
           (uint32_t)p[off+2]<<16|(uint32_t)p[off+3]<<24;
}
struct fixture {
    uint8_t config[256];
    alignas(8) uint8_t common[128];
    alignas(8) uint8_t notify[128];
    alignas(8) uint8_t device[128];
    struct nas_virtio_pci_resources res;
    struct nas_virtio_pci pci;
    struct nas_virtio_transport tr;
};
static void cap(uint8_t *cfg,unsigned offset,unsigned next,unsigned type,
                unsigned bar,unsigned size,unsigned multiplier) {
    cfg[offset]=9;cfg[offset+1]=(uint8_t)next;
    cfg[offset+2]=(uint8_t)(type==2?20:16);
    cfg[offset+3]=(uint8_t)type;cfg[offset+4]=(uint8_t)bar;
    p32(cfg,offset+8,0);p32(cfg,offset+12,size);
    if(type==2)p32(cfg,offset+16,multiplier);
}
static void prepare(struct fixture *f) {
    memset(f,0,sizeof(*f));
    p16(f->config,0,0x1af4);p16(f->config,2,0x1042);
    p16(f->config,6,0x10);f->config[0x34]=0x40;
    cap(f->config,0x40,0x50,1,0,0x40,0);
    cap(f->config,0x50,0x68,2,1,0x40,4);
    cap(f->config,0x68,0,4,2,0x40,0);
    f->res.config=f->config;
    f->res.bars[0]=(struct nas_virtio_pci_bar){f->common,sizeof(f->common)};
    f->res.bars[1]=(struct nas_virtio_pci_bar){f->notify,sizeof(f->notify)};
    f->res.bars[2]=(struct nas_virtio_pci_bar){f->device,sizeof(f->device)};
}
static int initialize(struct fixture *f) {
    static const struct nas_dma_provider dma={.coherent=true};
    return nas_virtio_pci_transport_init(&f->tr,&f->pci,&f->res,&dma);
}
int main(void) {
    struct fixture f;
    prepare(&f);
    assert(initialize(&f)==0);
    const struct nas_virtio_transport_ops *ops=f.tr.ops;
    assert(f.tr.context==&f.pci && ops);
    ops->write_status(&f.pci,7);
    assert(f.common[0x14]==7 && ops->read_status(&f.pci)==7);
    p32(f.common,4,0xdeadbeef);
    assert(ops->read_features(&f.pci,1)==0xdeadbeef);
    assert(g32(f.common,0)==1);
    ops->write_features(&f.pci,0,0xaabbccdd);
    assert(g32(f.common,8)==0 && g32(f.common,0x0c)==0xaabbccdd);
    p32(f.device,0,0x12345678);
    assert(ops->read_config32(&f.pci,0)==0x12345678);
    assert(ops->read_config32(&f.pci,2)==0);
    assert(ops->read_config32(&f.pci,0x40)==0);
    p16(f.common,0x12,1);p16(f.common,0x18,8);p16(f.common,0x1e,3);
    assert(ops->setup_queue(&f.pci,1,8,0x1000,0x2000,0x3000)==-EINVAL);
    assert(ops->setup_queue(&f.pci,0,16,0x1000,0x2000,0x3000)==-EOPNOTSUPP);
    assert(ops->setup_queue(&f.pci,0,8,0x1234567800001000ULL,
                            0x2000,0x3000)==0);
    assert(g16(f.common,0x16)==0 && g16(f.common,0x18)==8);
    assert(g16(f.common,0x1c)==1);
    assert(g32(f.common,0x20)==0x1000 && g32(f.common,0x24)==0x12345678);
    assert(g32(f.common,0x28)==0x2000 && g32(f.common,0x30)==0x3000);
    ops->notify_queue(&f.pci,0);
    assert(g16(f.notify,12)==0);
    p16(f.notify,12,0x5a5a);
    ops->notify_queue(&f.pci,1);
    assert(f.common[0x14] & 0x80);
    assert(g16(f.notify,12)==0x5a5a);
    ops->reset(&f.pci);
    assert(f.common[0x14]==0 && f.pci.queue_notify[0]==NULL);

    prepare(&f);f.config[0]=0;
    assert(initialize(&f)==-ENODEV);
    prepare(&f);f.config[6]=0;
    assert(initialize(&f)==-ENODEV);
    prepare(&f);f.config[0x50+1]=0x40;
    assert(initialize(&f)==-EINVAL); /* capability cycle */
    prepare(&f);f.config[0x50+2]=16;
    assert(initialize(&f)==-EINVAL); /* notify multiplier missing */
    prepare(&f);f.config[0x68+3]=1;
    assert(initialize(&f)==-EINVAL); /* duplicate common */
    prepare(&f);f.config[0x50+4]=6;
    assert(initialize(&f)==-EINVAL); /* BAR index */
    prepare(&f);p32(f.config,0x50+12,200);
    assert(initialize(&f)==-EINVAL); /* BAR bounds */
    prepare(&f);f.config[0x50+1]=0;
    assert(initialize(&f)==-ENODEV); /* missing device config */
    prepare(&f);f.config[0x40+2]=15;
    assert(initialize(&f)==-ENODEV); /* malformed common cap omitted */
    prepare(&f);assert(initialize(&f)==0);
    ops=f.tr.ops;
    p16(f.common,0x12,1);p16(f.common,0x18,8);
    p16(f.common,0x1e,17); /* 17*4 beyond 64-byte notify cap */
    assert(ops->setup_queue(&f.pci,0,8,0x1000,0x2000,0x3000)==-EOPNOTSUPP);
    assert(g16(f.common,0x1c)==0);
    puts("[VIRTIO MODERN PCI TRANSPORT PASS] capabilities, BAR validation, config, queue and notify");
    return 0;
}
