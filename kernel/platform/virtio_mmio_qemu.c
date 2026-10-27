/* Single transport/device enumeration for QEMU virt modern VirtIO-MMIO.
 * All block requests use ports/nuttx/virtio_blk.c; no ISA-specific driver.
 * Platform-specific page mapping and physical BAR are input parameters.
 */
#include "virtio_mmio_qemu.h"
#include "../drivers/core/bus.h"
#include "../drivers/virtio/mmio.h"
#include "../drivers/port/dma.h"
#include "../drivers/serial.h"
#include "../../ports/nuttx/virtio_bus.h"
#include <stdint.h>
#include <stdbool.h>
#define VIRTIO_MMIO_STRIDE 0x200U
#define VIRTIO_MMIO_SLOTS 32U
#define PAGE_BYTES 4096U
static struct nas_bus_device devices[VIRTIO_MMIO_SLOTS];
static struct nas_virtio_transport transports[VIRTIO_MMIO_SLOTS];
static bool scanned;
int nas_qemu_virtio_mmio_probe(uint64_t physical, uint64_t virtual_base,
                              bool (*map_page)(uint64_t,uint64_t)) {
    if (!map_page || (physical&4095U) || (virtual_base&4095U) || scanned)
        return -1;
    int rc=nas_nuttx_virtio_blk_driver_register();
    if (rc<0) return rc;
    for (unsigned page=0;page<4;page++)
        if (!map_page(physical+page*PAGE_BYTES,
                      virtual_base+page*PAGE_BYTES)) return -1;
    unsigned modern=0,legacy=0;
    for (unsigned slot=0;slot<VIRTIO_MMIO_SLOTS;slot++) {
        volatile uint32_t *reg=(volatile uint32_t *)(uintptr_t)
            (virtual_base+slot*VIRTIO_MMIO_STRIDE);
        if(reg[0]!=0x74726976U || reg[2]!=2U)continue;
        if(reg[1]!=2U){legacy++;continue;}
        modern++;
        rc=nas_virtio_mmio_transport_init(&transports[slot],
                                          reg,&nas_dma_qemu_coherent);
        if(rc<0)return rc;
        devices[slot]=(struct nas_bus_device){
            .bus=NAS_BUS_VIRTIO_MMIO,.device_id=reg[2],
            .vendor_id=reg[3],.resource=reg,.context=&transports[slot]
        };
        rc=nas_bus_device_register(&devices[slot]);
        if(rc<0)return rc;
    }
    nas_print("[VIRTIO-BLK] hardware transport modern=");
    nas_dec(modern);nas_print(" legacy=");nas_dec(legacy);nas_print("\n");
    nas_print("[VIRTIO-BLK] real modern MMIO devices=");
    nas_dec(nas_nuttx_virtio_blk_count());nas_print("\n");
    scanned=true;
    return (int)nas_nuttx_virtio_blk_count();
}
