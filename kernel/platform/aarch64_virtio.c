/* QEMU virt machine only: discover mapped modern VirtIO-MMIO devices.
 * Physical addresses must eventually come from firmware FDT/ACPI. The
 * transport/queue/BlockDevice layers contain NO platform addresses. */
#include <stdint.h>
#include <stdbool.h>
#include "../drivers/block/virtio_mmio_blk.h"
#include "../drivers/core/bus.h"
#include "../drivers/virtio/mmio.h"
#include "../drivers/port/dma.h"
#include "../drivers/serial.h"

extern bool nas_arm_map_io_page(uint64_t physical,uint64_t virtual_addr);
#define VIRTIO_MMIO_BASE UINT64_C(0x0a000000)
#define VIRTIO_IO_VIRT UINT64_C(0xffffffffe1000000)
#define VIRTIO_MMIO_STRIDE 0x200U
#define VIRTIO_MMIO_SLOTS 32U
static struct nas_bus_device mmio_devices[VIRTIO_MMIO_SLOTS];
static struct nas_virtio_transport mmio_transports[VIRTIO_MMIO_SLOTS];
int nas_arm_virtio_blk_probe(void){
    int driver_rc=nas_virtio_blk_driver_register();
    if(driver_rc<0)return driver_rc;
    for(unsigned page=0;page<4;page++)
        if(!nas_arm_map_io_page(VIRTIO_MMIO_BASE+page*4096U,
                                 VIRTIO_IO_VIRT+page*4096U))
            return -1;
    unsigned modern=0,legacy=0;
    for(unsigned slot=0;slot<VIRTIO_MMIO_SLOTS;slot++){
        volatile uint32_t *reg=(volatile uint32_t *)(uintptr_t)
             (VIRTIO_IO_VIRT+slot*VIRTIO_MMIO_STRIDE);
        if(reg[0]==0x74726976U && reg[2]==2U) {
            if(reg[1]==2U)modern++;
            else legacy++;
        }
        /* The platform publishes devices, never selects a storage driver.
         * Other drivers (net, vsock, GPU, ...) may bind to these slots. */
        if(reg[0]!=0x74726976U || reg[1]!=2U || !reg[2])continue;
        int trc=nas_virtio_mmio_transport_init(&mmio_transports[slot],
                                                reg,&nas_dma_qemu_coherent);
        if(trc<0)return trc;
        mmio_devices[slot]=(struct nas_bus_device){
            .bus=NAS_BUS_VIRTIO_MMIO,.device_id=reg[2],
            .vendor_id=reg[3],.resource=reg,.context=&mmio_transports[slot]
        };
        int rc=nas_bus_device_register(&mmio_devices[slot]);
        if(rc<0)return rc;
    }
    nas_print("[VIRTIO-BLK] hardware transport modern=");
    nas_dec(modern);nas_print(" legacy=");nas_dec(legacy);nas_print("\n");
    nas_print("[VIRTIO-BLK] real modern MMIO devices=");
    nas_dec(nas_virtio_blk_count());
    nas_print("\n");
    return (int)nas_virtio_blk_count();
}
