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
#define VIRTIO_MMIO_SLOTS_MAX 32U
#define PAGE_BYTES 4096U
static struct nas_bus_device devices[VIRTIO_MMIO_SLOTS_MAX];
static struct nas_virtio_transport transports[VIRTIO_MMIO_SLOTS_MAX];
static bool scanned;
int nas_qemu_virtio_mmio_probe(uint64_t physical, uint64_t virtual_base,
                              unsigned stride, unsigned slots,
                              bool (*map_page)(uint64_t,uint64_t)) {
    if (!map_page || (physical&4095U) || (virtual_base&4095U) ||
        !slots || slots>VIRTIO_MMIO_SLOTS_MAX || !stride ||
        (stride&3U) || stride<0x200U || stride>PAGE_BYTES ||
        (uint64_t)slots*stride>32U*PAGE_BYTES || scanned)
        return -1;
    int rc=nas_nuttx_virtio_blk_driver_register();
    if (rc<0) return rc;
    for (unsigned page=0;page<(slots*stride+PAGE_BYTES-1U)/PAGE_BYTES;page++)
        if (!map_page(physical+page*PAGE_BYTES,
                      virtual_base+page*PAGE_BYTES)) return -1;
    unsigned modern=0,legacy=0;
    for (unsigned slot=0;slot<slots;slot++) {
        volatile uint32_t *reg=(volatile uint32_t *)(uintptr_t)
            (virtual_base+slot*stride);
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
