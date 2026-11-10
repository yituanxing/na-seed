#include "x86_h3_nvme.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/nvme/identify.h"
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
#include "../drivers/nvme/readonly.h"
#include "../drivers/port/block.h"
#include "../block/device.h"
#endif
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two-page UC mapping includes NVMe registers and admin doorbells.
 * An isolated build and QEMU fw_cfg signature gate protect this experimental
 * write-capable test from the default physical-machine discovery image. */
#define H3_NVME_VIRT_BASE UINT64_C(0xfffffe8001100000)
#define H3_NVME_QEMU_BAR UINT64_C(0x000000c000000000)
#define H3_NVME_MAPPED_BYTES 8192U

struct h3_nvme_mmio {
    uint64_t virtual_base;
};

#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
/* H4's real read test uses the normal BlockDevice registry. The I/O queue,
 * DMA and port storage remain stable for the entire synchronous probe. */
static struct nas_nvme_ro_session h4_session;
static struct nas_port_block h4_port;
static uint8_t h4_sector[4096];
static const struct nas_port_block_ops h4_port_ops;

static int64_t h4_port_read(void *backend,uint64_t lba,
                             uint32_t count,void *dst) {
    int rc=nas_nvme_ro_read((struct nas_nvme_ro_session *)backend,lba,
                            count,dst);
    return rc ? rc : (int64_t)count;
}
static const struct nas_port_block_ops h4_port_ops={.read=h4_port_read};

static bool h4_expected_sector(unsigned which,const uint8_t *data) {
    static const char marker0[]="NASEED-H4-NVME-LBA0-20261110";
    static const char marker1[]="NASEED-H4-NVME-LBA1-20261110";
    const char *marker=which ? marker1 : marker0;
    unsigned size=(unsigned)(sizeof(marker0)-1U);
    for(unsigned i=0;i<512U;i++) {
        uint8_t expected=0;
        if(i<size)expected=(uint8_t)marker[i];
        if(i==511U)expected=(uint8_t)(which ? '1' : '0');
        if(data[i]!=expected)return false;
    }
    return true;
}

static int h4_readonly_block_proof(const struct nas_nvme_io *io) {
    int rc=nas_nvme_ro_open(io,&nas_dma_qemu_coherent,&h4_session);
    if(rc)return rc;
    bool attached=false;
    struct nas_block_device *block=NULL;
    if(h4_session.sector_bytes!=512U || h4_session.sectors<2U) {
        rc=-EOPNOTSUPP;
        goto finish;
    }
    rc=nas_port_block_attach(&h4_port,"h4nvme",&h4_session,
                              h4_session.sectors,h4_session.sector_bytes,
                              1U,true,NAS_PORT_BLOCK_SECTORS,&h4_port_ops);
    if(rc)goto finish;
    attached=true;
    block=nas_block_get("h4nvme");
    if(!block) {rc=-ENODEV;goto finish;}
    for(unsigned lba=0;lba<2U;lba++) {
        rc=nas_block_read(block,lba,1U,h4_sector);
        if(rc || !h4_expected_sector(lba,h4_sector)) {
            if(!rc)rc=-EIO;
            goto finish;
        }
    }
    nas_print("[H4 NVME LBA VERIFIED] NSID=1 LBA0+LBA1 exact fixture bytes\n");
    if(nas_block_write(block,0U,1U,h4_sector)!=-EROFS) {
        rc=-EIO;
        goto finish;
    }
    nas_print("[H4 BLOCK PORT VERIFIED] nas_block_read x2, writes=-EROFS\n");
finish:
    if(block)nas_block_put(block);
    if(attached) {
        int detach=nas_port_block_detach(&h4_port);
        if(detach && !rc)rc=detach;
    }
    /* If the controller cannot stop, its DMA allocations remain pinned. */
    int close_rc=nas_nvme_ro_close(&h4_session);
    if(close_rc && !rc)rc=close_rc;
    return rc;
}
#endif

static bool h3_qemu_fw_cfg_signature(void) {
    static const char signature[4] = { 'Q', 'E', 'M', 'U' };
    const uint16_t select_port=0x510U;
    const uint16_t data_port=0x511U;
    uint16_t selector=0U;
    __asm__ volatile("outw %0,%1" :: "a"(selector),"Nd"(select_port)
                     : "memory");
    for (unsigned i=0;i<4;i++) {
        uint8_t c=0;
        __asm__ volatile("inb %1,%0" : "=a"(c) : "Nd"(data_port)
                         : "memory");
        if (c!=(uint8_t)signature[i])return false;
    }
    return true;
}

static int h3_mmio_read32(void *ctx, uint32_t offset, uint32_t *value) {
    struct h3_nvme_mmio *mmio=ctx;
    if(!mmio || !value || (offset&3U) ||
       offset>H3_NVME_MAPPED_BYTES-4U)return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *value=*(volatile uint32_t *)(uintptr_t)(mmio->virtual_base+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static int h3_mmio_write32(void *ctx, uint32_t offset, uint32_t value) {
    struct h3_nvme_mmio *mmio=ctx;
    if(!mmio || (offset&3U) || offset>H3_NVME_MAPPED_BYTES-4U)
        return -EINVAL;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *(volatile uint32_t *)(uintptr_t)(mmio->virtual_base+offset)=value;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return 0;
}

static void h3_pci_config_write32(uint8_t bus,uint8_t slot,uint8_t function,
                                   uint16_t offset,uint32_t value) {
    uint32_t address=UINT32_C(0x80000000)|((uint32_t)bus<<16)|
                     ((uint32_t)slot<<11)|((uint32_t)function<<8)|
                     (uint32_t)(offset&0xfcU);
    uint16_t selector=0xcf8U, data=0xcfcU;
    __asm__ volatile("outl %0,%1" :: "a"(address),"Nd"(selector)
                     : "memory");
    __asm__ volatile("outl %0,%1" :: "a"(value),"Nd"(data)
                     : "memory");
}

int nas_x86_h3_nvme_identify(const struct nas_pci_function_info *function,
                             const struct nas_pci_resources *resources) {
    if(!function || !resources)return -EINVAL;
    /* The only permitted target is this exact QEMU-generated device/profile.
     * No generic physical NVMe BAR write support is implied by H3. */
    if(function->bus!=0U || function->slot!=3U || function->function!=0U ||
       function->vendor_id!=0x1b36U || function->device_id!=0x0010U ||
       function->class_code!=1U || function->subclass!=8U ||
       function->programming_interface!=2U ||
       !h3_qemu_fw_cfg_signature()) {
        nas_print("[H3 NVME FAIL] not the isolated QEMU controller\n");
        return -ENODEV;
    }
    const struct nas_pci_bar_desc *bar=NULL;
    for(unsigned i=0;i<resources->bar_count;i++) {
        if(resources->bars[i].index==0U) {
            bar=&resources->bars[i];
            break;
        }
    }
    if(!bar || bar->kind!=NAS_PCI_BAR_MEM64 ||
       bar->base!=H3_NVME_QEMU_BAR ||
       (bar->base&4095U) || bar->base>UINT64_MAX-H3_NVME_MAPPED_BYTES) {
        nas_print("[H3 NVME FAIL] unsupported/unqualified QEMU BAR0\n");
        return -ENODEV;
    }
    /* No unhandled guest interrupts can race the CF8 selector while the
     * experimental admin-queue device is active. This image halts afterward. */
    __asm__ volatile("cli" ::: "memory");
    for(unsigned i=0;i<H3_NVME_MAPPED_BYTES/4096U;i++) {
        if(!nas_arch_x86_map_device_page(bar->base+(uint64_t)i*4096U,
                                         H3_NVME_VIRT_BASE+(uint64_t)i*4096U)) {
            nas_print("[H3 NVME FAIL] cannot map QEMU BAR UC/RW/NX\n");
            return -EIO;
        }
    }
    /* PCI Command bits: memory-space decode + bus master. No I/O decode,
     * MSI, MSI-X, DMA remapping or actual namespace/block driver binding. */
    /* Upper PCI Status bits are W1C. Never echo their readback into the
     * configuration DWORD, or the diagnostic would clear error status. */
    const uint32_t before=(uint32_t)resources->command;
    h3_pci_config_write32(function->bus,function->slot,function->function,
                          4U,before|UINT32_C(0x00000006));
    struct h3_nvme_mmio mmio={.virtual_base=H3_NVME_VIRT_BASE};
    const struct nas_nvme_io io={
        .context=&mmio,.mmio_bytes=H3_NVME_MAPPED_BYTES,
        .read32=h3_mmio_read32,.write32=h3_mmio_write32
    };
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
    int rc=h4_readonly_block_proof(&io);
#else
    struct nas_nvme_identity id={0};
    int rc=nas_nvme_identify_controller(&io,&nas_dma_qemu_coherent,&id);
#endif
    /* Reset only the PCI command flags enabled by H3. The NVMe core must
     * disable/quiesce admin DMA before returning, even on error. */
    h3_pci_config_write32(function->bus,function->slot,function->function,
                          4U,before);
    if(rc) {
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
        nas_print("[H4 NVME FAIL] read-only LBA/block path error=");
#else
        nas_print("[H3 NVME FAIL] Identify admin queue error=");
#endif
        nas_dec((unsigned)(-rc));nas_print("\n");
        return rc;
    }
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
    nas_print("[H4 NVME] QEMU namespace read-only proof; LBA writes=0\n");
#else
    nas_print("[H3 NVME IDENTIFY VERIFIED] VS=");nas_hex(id.version);
    nas_print(" VID=");nas_hex(id.vendor_id);
    nas_print(" ControllerNN(raw)=");nas_dec(id.controller_nn);
    nas_print(" SN=");nas_print(id.serial);
    nas_print(" MN=");nas_print(id.model);
    nas_print("\n");
    nas_print("[H3 NVME] no namespace block attach; no LBA I/O\n");
#endif
    return 0;
}
