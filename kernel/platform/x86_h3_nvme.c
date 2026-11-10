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
#ifdef NAS_ENABLE_H5_QEMU_PCI_RESOURCE
/* Isolated Q35 fixture's high 64-bit PCI MMIO allocation window, not a
 * portable x86 host bridge range or an independently measured BAR aperture.
 * H5 still requires fw_cfg + unique QEMU NVMe device + explicit QEMU build. */
#define H5_QEMU_MMIO64_BASE UINT64_C(0x000000c000000000)
#define H5_QEMU_MMIO64_BYTES UINT64_C(0x0000000100000000)
#ifdef NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME
/* One-shot QEMU platform owner. H0-H6 deliberately keep mapped claims pinned;
 * H7 may reclaim one only after an explicitly verified DMA-safe unmap.
 * Never substitute this Q35 fixture window on a physical host bridge. */
static struct nas_pci_mmio_owner h6_qemu_mmio_owner={
    .window_base=H5_QEMU_MMIO64_BASE,
    .window_bytes=H5_QEMU_MMIO64_BYTES
};
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
/* Lifetime extends across filesystem mount and genuine userspace execution.
 * The only release path is explicit H7 unmount -> controller quiesce -> DMA
 * lease teardown -> PCI command restore -> checked UC BAR unmap/release. */
static struct {
    bool attached;
    uint8_t bus,slot,function;
    uint16_t pci_command_before;
    struct nas_pci_mmio_claim claim;
} h7_persistent;
#endif
#endif
#endif

struct h3_nvme_mmio {
    uint64_t virtual_base;
};

#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
/* H4's real read test uses the normal BlockDevice registry. The I/O queue,
 * DMA and port storage remain stable for the entire synchronous probe. */
static struct nas_nvme_ro_session h4_session;
static struct nas_port_block h4_port;
#ifndef NAS_ENABLE_H7_QEMU_NVME_USERLAND
static uint8_t h4_sector[4096];
#endif
static const struct nas_port_block_ops h4_port_ops;

static int64_t h4_port_read(void *backend,uint64_t lba,
                             uint32_t count,void *dst) {
    int rc=nas_nvme_ro_read((struct nas_nvme_ro_session *)backend,lba,
                            count,dst);
    return rc ? rc : (int64_t)count;
}
static const struct nas_port_block_ops h4_port_ops={.read=h4_port_read};

#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
static int h7_attach_persistent_block(const struct nas_nvme_io *io) {
    if(h7_persistent.attached || h4_session.io || h4_port.block.registered)
        return -EBUSY;
    int rc=nas_nvme_ro_open(io,&nas_dma_qemu_coherent,&h4_session);
    if(rc)return rc;
    if(h4_session.sector_bytes!=512U || h4_session.sectors<16384U) {
        rc=-EOPNOTSUPP;
        goto failed;
    }
    rc=nas_port_block_attach(&h4_port,"nvme0",&h4_session,
                              h4_session.sectors,h4_session.sector_bytes,
                              8U,true,NAS_PORT_BLOCK_SECTORS,&h4_port_ops);
    if(rc)goto failed;
    h7_persistent.attached=true;
    nas_print("[H7 NVME BLOCK ATTACHED] name=nvme0 read-only sectors=");
    nas_dec((unsigned)h4_session.sectors);
    nas_print(" sector_bytes=");nas_dec(h4_session.sector_bytes);
    nas_print(" PCI bus master and DMA lease retained until shutdown\n");
    return 0;
failed:
    /* Never claim a safe release if the device's disable/RDY check fails;
     * the live lease remains pinned for the failed boot to halt safely. */
    (void)nas_nvme_ro_close(&h4_session);
    return rc;
}
#endif

#ifndef NAS_ENABLE_H7_QEMU_NVME_USERLAND
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

#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
static void h7_restore_interrupts(uint64_t saved_rflags) {
    if(saved_rflags&(UINT64_C(1)<<9))
        __asm__ volatile("sti" ::: "memory");
}

int nas_x86_h7_nvme_shutdown(void) {
    if(!h7_persistent.attached)return -ENODEV;
    int rc=nas_port_block_detach(&h4_port);
    if(rc)return rc; /* VFS/FD still pins the block device. */
    rc=nas_nvme_ro_close(&h4_session);
    if(rc)return rc; /* Controller may still DMA: do not release BAR. */
    /* Restore exactly the Command bits which were present before H7 bound.
     * A successful close proved CSTS.RDY=0 and freed every leased page. */
    h3_pci_config_write32(h7_persistent.bus,h7_persistent.slot,
                           h7_persistent.function,4U,
                           h7_persistent.pci_command_before);
    rc=nas_pci_mmio_quiesced_unmap_release(
        &h6_qemu_mmio_owner,&h7_persistent.claim,H3_NVME_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(rc)return rc;
    h7_persistent.attached=false;
    nas_print("[H7 NVME QUIESCED RELEASED] CSTS.RDY=0 DMA pages freed BAR UC unmapped PCI claim released\n");
    return 0;
}
#endif

int nas_x86_h3_nvme_identify(const struct nas_pci_function_info *function,
                             const struct nas_pci_resources *resources) {
    if(!function || !resources)return -EINVAL;
    /* The only permitted target is this exact QEMU-generated device/profile.
     * No generic physical NVMe BAR write support is implied by H3. */
    if(function->vendor_id!=0x1b36U || function->device_id!=0x0010U ||
       function->class_code!=1U || function->subclass!=8U ||
       function->programming_interface!=2U ||
#ifndef NAS_ENABLE_H5_QEMU_PCI_RESOURCE
       function->bus!=0U || function->slot!=3U || function->function!=0U ||
#endif
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
    if(!bar || bar->kind!=NAS_PCI_BAR_MEM64 || bar->prefetchable ||
#ifndef NAS_ENABLE_H5_QEMU_PCI_RESOURCE
       bar->base!=H3_NVME_QEMU_BAR ||
#endif
       (bar->base&4095U) || bar->base>UINT64_MAX-H3_NVME_MAPPED_BYTES ||
       bar->base>UINT64_C(0x000ffffffffff000)-H3_NVME_MAPPED_BYTES) {
        nas_print("[H3 NVME FAIL] unsupported/unqualified QEMU BAR0\n");
        return -ENODEV;
    }
#ifdef NAS_ENABLE_H5_QEMU_PCI_RESOURCE
#ifdef NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME
    struct nas_pci_mmio_owner *owner=&h6_qemu_mmio_owner;
#else
    struct nas_pci_mmio_owner local_owner={
        .window_base=H5_QEMU_MMIO64_BASE,
        .window_bytes=H5_QEMU_MMIO64_BYTES
    };
    struct nas_pci_mmio_owner *owner=&local_owner;
#endif
    struct nas_pci_mmio_claim claim={0};
    int claim_rc=nas_pci_mmio_claim_bar(owner,resources,0U,
                                       H3_NVME_MAPPED_BYTES,&claim);
    if(claim_rc) {
        nas_print("[H5 PCI BAR FAIL] outside bounded QEMU MMIO64 fixture window\n");
        return claim_rc;
    }
#endif
    /* No unhandled guest interrupts can race the CF8 selector during setup.
     * H3-H6 halt afterward; H7 restores its previous IF before userspace. */
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
    uint64_t h7_saved_flags=0;
    __asm__ volatile("pushfq; popq %0" : "=r"(h7_saved_flags) :: "memory");
#endif
    __asm__ volatile("cli" ::: "memory");
#ifdef NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME
    /* Pin the claim BEFORE even the first mapping attempt. A failed MMU
     * call may have installed a partial PTE; releasing or reassigning this
     * physical resource without unmapping would be unsafe. */
    int pin_rc=nas_pci_mmio_pin_mapping_attempt(owner,&claim);
    if(pin_rc) {
        nas_print("[H6 PCI MMIO FAIL] cannot pin mapping ownership\n");
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
        h7_restore_interrupts(h7_saved_flags);
#endif
        return pin_rc;
    }
#endif
    for(unsigned i=0;i<H3_NVME_MAPPED_BYTES/4096U;i++) {
        if(!nas_arch_x86_map_device_page(bar->base+(uint64_t)i*4096U,
                                         H3_NVME_VIRT_BASE+(uint64_t)i*4096U)) {
            nas_print("[H3 NVME FAIL] cannot map QEMU BAR UC/RW/NX\n");
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
            h7_restore_interrupts(h7_saved_flags);
#endif
            return -EIO;
        }
    }
#ifdef NAS_ENABLE_H5_QEMU_PCI_RESOURCE
    nas_print("[H5 PCI BAR QUALIFIED] bus=");nas_dec(function->bus);
    nas_print(" slot=");nas_dec(function->slot);
    nas_print(" function=");nas_dec(function->function);
    nas_print(" BAR0=");nas_hex(claim.base);
    nas_print(" extent=8192 QEMU fixture only; true BAR size unknown\n");
#endif
#ifdef NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME
    nas_print("[H6 PCI MMIO PINNED] bus=");nas_dec(function->bus);
    nas_print(" slot=");nas_dec(function->slot);
    nas_print(" function=");nas_dec(function->function);
    nas_print(" BAR0=");nas_hex(claim.base);
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
    nas_print(" until H7 userspace unmount and verified quiesced unmap\n");
#else
    nas_print(" until UC pages are explicitly unmapped (not implemented)\n");
#endif
#endif
    /* PCI Command bits: memory-space decode + bus master. No I/O decode,
     * MSI, MSI-X or DMA remapping. H7 alone registers a live BlockDevice. */
    /* Upper PCI Status bits are W1C. Never echo their readback into the
     * configuration DWORD, or the diagnostic would clear error status. */
    const uint32_t before=(uint32_t)resources->command;
    h3_pci_config_write32(function->bus,function->slot,function->function,
                          4U,before|UINT32_C(0x00000006));
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
    /* Both MMIO and IO descriptor MUST outlive this function: ext4 and real
     * ELF reads arrive asynchronously after PCI inventory has returned. */
    static struct h3_nvme_mmio h7_mmio;
    static struct nas_nvme_io h7_io;
    h7_mmio.virtual_base=H3_NVME_VIRT_BASE;
    h7_io=(struct nas_nvme_io){
        .context=&h7_mmio,.mmio_bytes=H3_NVME_MAPPED_BYTES,
        .read32=h3_mmio_read32,.write32=h3_mmio_write32
    };
    h7_persistent.claim=claim;
    h7_persistent.bus=function->bus;
    h7_persistent.slot=function->slot;
    h7_persistent.function=function->function;
    h7_persistent.pci_command_before=resources->command;
    int rc=h7_attach_persistent_block(&h7_io);
    h7_restore_interrupts(h7_saved_flags);
#else
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
#endif
    /* Reset only the PCI command flags enabled by H3. The NVMe core must
     * disable/quiesce admin DMA before returning, even on error. */
#ifndef NAS_ENABLE_H7_QEMU_NVME_USERLAND
    h3_pci_config_write32(function->bus,function->slot,function->function,
                          4U,before);
#endif
#if defined(NAS_ENABLE_H5_QEMU_PCI_RESOURCE) && !defined(NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME)
    if(!rc && nas_pci_mmio_release(owner,&claim))rc=-EIO;
#endif
    if(rc) {
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
        /* Only an already quiesced, fully closed failed setup may restore
         * the original Command. A failed CSTS.RDY wait preserves the lease
         * and BUS MASTER state to avoid silently invalidating live DMA. */
        if(!h4_session.may_dma && !h4_session.io)
            h3_pci_config_write32(function->bus,function->slot,
                                   function->function,4U,before);
#endif
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
        nas_print("[H7 NVME FAIL] persistent read-only block path error=");
#else
        nas_print("[H4 NVME FAIL] read-only LBA/block path error=");
#endif
#else
        nas_print("[H3 NVME FAIL] Identify admin queue error=");
#endif
        nas_dec((unsigned)(-rc));nas_print("\n");
        return rc;
    }
#ifdef NAS_ENABLE_H4_QEMU_NVME_READONLY
#ifdef NAS_ENABLE_H7_QEMU_NVME_USERLAND
    nas_print("[H7 NVME] live read-only ext4 backing via nvme0; no LBA writes\n");
#else
    nas_print("[H4 NVME] QEMU namespace read-only proof; LBA writes=0\n");
#endif
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
