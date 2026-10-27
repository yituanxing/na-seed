/* QEMU virt machine only: discover mapped modern VirtIO-MMIO devices.
 * Physical addresses must eventually come from firmware FDT/ACPI. The
 * transport/queue/BlockDevice layers contain NO platform addresses. */
#include <stdint.h>
#include <stdbool.h>
#include "../drivers/block/virtio_mmio_blk.h"
#include "../drivers/serial.h"

extern bool nas_arm_map_io_page(uint64_t physical,uint64_t virtual_addr);
#define VIRTIO_MMIO_BASE UINT64_C(0x0a000000)
#define VIRTIO_IO_VIRT UINT64_C(0xffffffffe1000000)
#define VIRTIO_MMIO_STRIDE 0x200U
#define VIRTIO_MMIO_SLOTS 32U
int nas_arm_virtio_blk_probe(void){
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
        int rc=nas_virtio_mmio_blk_attach(reg);
        if(rc<0)return rc;
    }
    nas_print("[VIRTIO-BLK] hardware transport modern=");
    nas_dec(modern);nas_print(" legacy=");nas_dec(legacy);nas_print("\n");
    nas_print("[VIRTIO-BLK] real modern MMIO devices=");
    nas_dec(nas_virtio_mmio_blk_count());
    nas_print("\n");
    return (int)nas_virtio_mmio_blk_count();
}
