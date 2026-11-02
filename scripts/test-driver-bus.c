#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/drivers/core/bus.h"
#include "../kernel/drivers/port/block.h"
#include "../kernel/task/scheduler_service.h"
#include "../kernel/task/kthread.h"
#include "../kernel/task/sched_core.h"
#include "../kernel/errno.h"
/* Exercise the real scheduler's deferred bus call without creating a
 * kernel context, a physical clock or an unrelated runnable user Task. */
uint64_t nas_arch_monotonic_ns(void){return 0;}
unsigned nas_kthread_timers_tick(uint64_t t){(void)t;return 0;}
bool nas_kthread_runnable(void){return false;}
bool nas_sched_has_ready_users(void){return true;}
bool nas_sched_allow_kernel_slice(bool u,bool k){(void)u;(void)k;return false;}
unsigned nas_kthread_run(unsigned budget){(void)budget;return 0;}
static uint8_t storage[4096];
static struct nas_port_block port;
static unsigned probe_count, remove_count, fallback_count;
static int64_t rd(void *c, uint64_t sector, uint32_t count, void *dst) {
    (void)c;
    if(sector+count>8) return -EIO;
    uint8_t *p=dst;
    for(uint32_t n=0;n<count*512U;n++)p[n]=storage[sector*512U+n];
    return count;
}
static int64_t wr(void *c, uint64_t sector, uint32_t count, const void *src) {
    (void)c;
    if(sector+count>8)return -EIO;
    uint8_t *p=storage+sector*512U;
    const uint8_t *s=src;
    for(uint32_t n=0;n<count*512U;n++)p[n]=s[n];
    return count;
}
static int fl(void *ctx){(void)ctx;return 0;}
static const struct nas_port_block_ops backend={.read=rd,.write=wr,.flush=fl};
static bool match(const struct nas_bus_device *dev) {
    return dev->vendor_id==0x1af4 && dev->device_id==2;
}
static int probe(struct nas_bus_device *dev) {
    probe_count++;
    return nas_port_block_attach(&port,"portedbus",dev,8,512,4,false,
                                 NAS_PORT_BLOCK_SECTORS,&backend);
}
static int remove_cb(struct nas_bus_device *dev) {
    (void)dev;
    remove_count++;
    return nas_port_block_detach(&port);
}
static int ignored(struct nas_bus_device *dev) {(void)dev;fallback_count++;return -ENODEV;}
static const struct nas_bus_driver fake={.name="virtio-blk-upstream",
    .bus_mask=NAS_BUS_MASK_ALL,.match=match,.probe=probe,.remove=remove_cb};
static const struct nas_bus_driver pci={.name="pci-unclaimed",
    .bus_mask=NAS_BUS_MASK_PCI,.probe=ignored};
/* Type 1 (VirtIO network) must be independently claimable on BOTH buses;
 * it must never be offered to the block-class backend. The live Ethernet
 * driver is a separate upstream port, not part of this binding test. */
static unsigned net_probe_count,net_remove_count;
static int net_remove_result;
static bool net_match(const struct nas_bus_device *d){
    return d->vendor_id==0x1af4 && d->device_id==1;
}
static int net_probe(struct nas_bus_device *d){
    (void)d;net_probe_count++;return 0;
}
static int net_remove(struct nas_bus_device *d){
    (void)d;net_remove_count++;return net_remove_result;
}
static const struct nas_bus_driver net={.name="virtio-net-class",
    .bus_mask=NAS_BUS_MASK_ALL,.match=net_match,
    .probe=net_probe,.remove=net_remove};
static struct nas_bus_device net_mmio={.bus=NAS_BUS_VIRTIO_MMIO,
    .vendor_id=0x1af4,.device_id=1,.resource=(volatile void*)0x3000};
static struct nas_bus_device net_pci={.bus=NAS_BUS_PCI,
    .vendor_id=0x1af4,.device_id=1,.resource=(volatile void*)0x4000};
static struct nas_bus_device dev={.bus=NAS_BUS_VIRTIO_MMIO,
    .vendor_id=0x1af4,.device_id=2,.resource=(volatile void*)0x1000};
static struct nas_bus_device unused={.bus=NAS_BUS_PCI,
    .vendor_id=0x8086,.device_id=0x5845,.resource=(volatile void*)0x2000};
static struct nas_bus_device unclaimed={.bus=NAS_BUS_PCI,
    .vendor_id=0x8086,.device_id=0xffff,.resource=(volatile void*)0x5000};
int main(void) {
    assert(nas_bus_device_remove_status(&unclaimed)==0);
    assert(nas_bus_device_register(&unclaimed)==0 && !unclaimed.driver);
    assert(nas_bus_device_remove_status(&unclaimed)==-EAGAIN);
    assert(nas_bus_device_request_remove(&unclaimed)==0);
    assert(nas_bus_device_remove_status(&unclaimed)==0 &&
           !unclaimed.registered);
    assert(nas_bus_device_request_remove(&unclaimed)==-ENODEV);
    assert(nas_bus_device_register(&dev)==0 && dev.registered && !dev.driver);
    assert(nas_bus_device_register(&unused)==0 && !unused.driver);
    assert(nas_bus_driver_register(&pci)==0); /* PCI probe declines */
    assert(!unused.driver && fallback_count==1);
    assert(nas_bus_driver_register(&fake)==0);
    assert(dev.driver==&fake && probe_count==1);
    assert(nas_bus_driver_register(&fake)==-EEXIST);
    assert(nas_bus_device_register(&net_mmio)==0 && !net_mmio.driver);
    assert(nas_bus_device_register(&net_pci)==0 && !net_pci.driver);
    assert(nas_bus_driver_register(&net)==0);
    assert(net_mmio.driver==&net && net_pci.driver==&net);
    assert(net_probe_count==2 && probe_count==1);
    assert(nas_bus_driver_unregister(&net)==-EBUSY);
    /* Two simultaneous removals: both begin once, no new probe/rebind,
     * and the scheduler drains only one device per service budget. */
    net_remove_result=-EBUSY;
    assert(nas_bus_device_request_remove(&net_mmio)==0);
    assert(nas_bus_device_request_remove(&net_pci)==0);
    assert(nas_bus_device_request_remove(&net_mmio)==0);
    assert(net_remove_count==2);
    assert(nas_bus_device_remove_status(&net_mmio)==-EINPROGRESS);
    assert(nas_bus_device_unregister(&net_mmio)==-EBUSY);
    assert(nas_bus_rescan()==0 && net_probe_count==2);
    assert(nas_bus_service_removals(1)==1);
    assert(net_remove_count==3 && net_mmio.registered && net_pci.registered);
    assert(nas_bus_service_removals(1)==1 && net_remove_count==4);
    net_remove_result=0;
    assert(nas_bus_service_removals(2)==2);
    assert(!net_mmio.registered && !net_pci.registered &&
           !net_mmio.driver && !net_pci.driver);
    assert(nas_bus_device_remove_status(&net_mmio)==0 &&
           nas_bus_device_remove_status(&net_pci)==0);
    assert(nas_bus_service_removals(1)==0);
    /* A terminal remove error is visible; a fresh explicit request may
     * retry, but the bus never loops endlessly on a broken driver. */
    assert(nas_bus_device_register(&net_mmio)==0);
    net_remove_result=-EBUSY;
    assert(nas_bus_device_request_remove(&net_mmio)==0);
    net_remove_result=-EIO;
    assert(nas_bus_service_removals(1)==1);
    assert(nas_bus_device_remove_status(&net_mmio)==-EIO);
    assert(net_mmio.registered && net_mmio.driver==&net);
    assert(nas_bus_service_removals(1)==0);
    net_remove_result=0;
    assert(nas_bus_device_request_remove(&net_mmio)==0);
    assert(nas_bus_device_remove_status(&net_mmio)==0);
    assert(nas_bus_driver_unregister(&net)==0);
    struct nas_block_device *pin=nas_block_get("portedbus");
    assert(pin==&port.block);
    assert(nas_bus_device_unbind(&dev)==-EBUSY);
    assert(dev.driver==&fake && remove_count==1);
    assert(nas_bus_driver_unregister(&fake)==-EBUSY);
    uint8_t b[512]={42};
    assert(nas_block_write(pin,2,1,b)==0);
    nas_block_put(pin);
    assert(nas_bus_device_unbind(&dev)==0);
    assert(dev.driver==NULL && nas_block_get("portedbus")==NULL);
    /* Prove one and the same block driver can bind a second PCI
     * transport with no architecture-specific storage implementation. */
    /* The MMIO source disappears before a separate PCI instance appears,
     * otherwise rescan would correctly rebind the old MMIO device first. */
    assert(nas_bus_device_unregister(&dev)==0);
    unused.vendor_id=0x1af4;unused.device_id=2;
    assert(nas_bus_rescan()==0);
    assert(unused.driver==&fake && probe_count==2);
    struct nas_block_device *other=nas_block_get("portedbus");
    assert(other==&port.block);
    /* A bound filesystem may keep doing I/O even when a bus remove was
     * requested. The normal scheduler reaps the device after unmount. */
    assert(nas_bus_device_request_remove(&unused)==0);
    assert(nas_bus_device_remove_status(&unused)==-EINPROGRESS);
    assert(port.block.removing && nas_block_get("portedbus")==NULL);
    assert(nas_bus_rescan()==0 && probe_count==2);
    nas_sched_service_kernel(); /* actual common Task dispatcher hook */
    assert(nas_bus_device_remove_status(&unused)==-EINPROGRESS);
    uint8_t retained[512]={0};
    assert(nas_block_read(other,2,1,retained)==0 && retained[0]==42);
    nas_block_put(other);
    nas_sched_service_kernel(); /* no explicit unbind retry from the caller */
    assert(nas_bus_device_remove_status(&unused)==0 && !unused.registered);
    assert(nas_block_get("portedbus")==NULL);
    assert(nas_bus_driver_unregister(&fake)==0);
    assert(nas_bus_rescan()==0 && !dev.driver && !unused.driver);
    assert(nas_bus_driver_unregister(&pci)==0);
    puts("[DRIVER BUS PASS] late binding, bounded async remove, failure retry, independent NICs, retained mounted block I/O, no rebind");
    return 0;
}
