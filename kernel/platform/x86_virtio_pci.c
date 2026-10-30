/* QEMU q35 root-bus PCI #1 enumerator. x86 owns config I/O and PTE
 * mapping ONLY; existing PCI transport, bus and original U-Boot request
 * implementation remain shared. This intentionally probes bus 0 (the
 * explicitly configured QEMU root device), not arbitrary PCIe topology. */
#include "../drivers/virtio/pci.h"
#include "../drivers/core/bus.h"
#include "../drivers/port/dma.h"
#include "../arch/x86_64/mmu.h"
#include "../../ports/uboot/virtio_bus.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdint.h>
#include <stdbool.h>
#define DISKS 4U
#define BAR_COUNT 6U
#define BAR_MAX_BYTES 65536U
#define PCI_MMIO_BASE UINT64_C(0xfffffe9000000000)
#define DISK_VA_STRIDE (BAR_COUNT*BAR_MAX_BYTES)
static uint8_t cfg_snapshot[DISKS][256];
static struct nas_virtio_pci_resources resources[DISKS];
static struct nas_virtio_pci pci[DISKS];
static struct nas_virtio_transport transports[DISKS];
static struct nas_bus_device devices[DISKS];
static void out32(uint16_t port,uint32_t value) {
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));
}
static uint32_t in32(uint16_t port) {
    uint32_t value;__asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port));
    return value;
}
static uint32_t cfg_read(uint8_t bus,uint8_t slot,uint8_t fun,
                         unsigned offset) {
    out32(0xcf8,UINT32_C(0x80000000) | ((uint32_t)bus<<16) |
        ((uint32_t)slot<<11) | ((uint32_t)fun<<8) | (offset&0xfcU));
    return in32(0xcfc);
}
static void cfg_write(uint8_t bus,uint8_t slot,uint8_t fun,
                      unsigned offset,uint32_t value) {
    out32(0xcf8,UINT32_C(0x80000000) | ((uint32_t)bus<<16) |
        ((uint32_t)slot<<11) | ((uint32_t)fun<<8) | (offset&0xfcU));
    out32(0xcfc,value);
}
static void load_config(uint8_t bus,uint8_t slot,uint8_t fun,uint8_t *cfg) {
    for(unsigned off=0;off<256;off+=4) {
        uint32_t value=cfg_read(bus,slot,fun,off);
        for(unsigned i=0;i<4;i++)cfg[off+i]=(uint8_t)(value>>(i*8));
    }
}
static bool map_bars(uint8_t bus,uint8_t slot,uint8_t fun,unsigned disk,
                     struct nas_virtio_pci_resources *res) {
    /* Disable decode while sizing, then enable memory + bus mastering
     * only for the device whose BAR regions passed bounds checks. */
    uint32_t old_cmd=cfg_read(bus,slot,fun,4);
    cfg_write(bus,slot,fun,4,old_cmd&~7U);
    bool mapped=false;
    for(unsigned i=0;i<BAR_COUNT;i++) {
        unsigned off=0x10U+i*4U;
        uint32_t lo=cfg_read(bus,slot,fun,off);
        if(!lo || lo==UINT32_MAX || (lo&1U))continue;
        unsigned type=(lo>>1)&3U;
        if(type!=0U && type!=2U)continue;
        if(type==2U && i+1U>=BAR_COUNT)continue;
        uint32_t hi=type==2U?cfg_read(bus,slot,fun,off+4):0;
        uint64_t address=((uint64_t)hi<<32)|(lo&~UINT32_C(15));
        cfg_write(bus,slot,fun,off,UINT32_MAX);
        if(type==2U)cfg_write(bus,slot,fun,off+4,UINT32_MAX);
        uint32_t probelo=cfg_read(bus,slot,fun,off);
        uint32_t probehi=type==2U?cfg_read(bus,slot,fun,off+4):UINT32_MAX;
        cfg_write(bus,slot,fun,off,lo);
        if(type==2U)cfg_write(bus,slot,fun,off+4,hi);
        uint64_t mask=((uint64_t)probehi<<32)|(probelo&~UINT32_C(15));
        uint64_t size=~mask+1U;
        if(address && !(address&4095U) && size>=4096U &&
           size<=BAR_MAX_BYTES && !(size&(size-1U))) {
            uint64_t virt=PCI_MMIO_BASE+(uint64_t)disk*DISK_VA_STRIDE+
                          (uint64_t)i*BAR_MAX_BYTES;
            bool ok=true;
            for(uint64_t pg=0;pg<size;pg+=4096U)
                if(!nas_arch_x86_map_device_page(address+pg,virt+pg)){
                    ok=false;break;
                }
            if(ok) {
                res->bars[i].cpu=(volatile uint8_t *)(uintptr_t)virt;
                res->bars[i].size=size;
                mapped=true;
            }
        }
        if(type==2U)i++;
    }
    cfg_write(bus,slot,fun,4,mapped?old_cmd|6U:old_cmd);
    return mapped;
}
int nas_platform_virtio_blk_probe(void) {
    int rc=nas_virtio_blk_driver_register();
    if(rc<0)return rc;
    unsigned count=0;
    for(unsigned slot=0;slot<32U;slot++) {
        uint32_t first=cfg_read(0,(uint8_t)slot,0,0);
        if((first&0xffffU)==0xffffU)continue;
        uint32_t header=cfg_read(0,(uint8_t)slot,0,0x0c);
        unsigned funcs=(header&(1U<<23))?8U:1U;
        for(unsigned function=0;function<funcs;function++) {
            uint32_t id=cfg_read(0,(uint8_t)slot,(uint8_t)function,0);
            if((id&0xffffU)!=0x1af4U || (id>>16)!=0x1042U)continue;
            if(count>=DISKS)return -ENOSPC;
            load_config(0,(uint8_t)slot,(uint8_t)function,
                        cfg_snapshot[count]);
            resources[count].config=cfg_snapshot[count];
            if(!map_bars(0,(uint8_t)slot,(uint8_t)function,count,
                         &resources[count])) {
                nas_print("[VIRTIO PCI FAIL] stage=bar-map slot=");
                nas_dec(slot);nas_print(" function=");nas_dec(function);
                nas_print("\n");return -ENODEV;
            }
            rc=nas_virtio_pci_transport_init(&transports[count],&pci[count],
                                             &resources[count],
                                             &nas_dma_qemu_coherent);
            if(rc) {
                nas_print("[VIRTIO PCI FAIL] stage=caps error=");
                nas_dec((uint64_t)(-rc));nas_print(" bar-sizes=");
                for(unsigned b=0;b<BAR_COUNT;b++){
                    nas_dec(resources[count].bars[b].size);nas_print(",");
                }
                nas_print("\n");return rc;
            }
            devices[count]=(struct nas_bus_device){
                .bus=NAS_BUS_PCI,.device_id=2,.vendor_id=id>>16,
                .resource=&resources[count],.context=&transports[count]
            };
            rc=nas_bus_device_register(&devices[count]);
            if(rc) {
                nas_print("[VIRTIO PCI FAIL] stage=driver-bind error=");
                nas_dec((uint64_t)(-rc));nas_print("\n");return rc;
            }
            count++;
        }
    }
    nas_print("[VIRTIO-BLK] real modern PCI devices=");
    nas_dec(nas_virtio_blk_count());nas_print("\n");
    return (int)nas_virtio_blk_count();
}
