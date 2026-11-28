#include "x86_h9_ahci.h"
#include "../arch/x86_64/mmu.h"
#include "x86_qemu_hw_contract.h"
#include "../drivers/ahci/readonly.h"
#include "../drivers/port/block.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* QEMU q35's built-in Intel ICH9 AHCI function at 00:1f.2 has a 4 KiB
 * BAR5 AHCI aperture. It is NOT the 64-bit NVMe BAR0 and this profile must
 * never use that unrelated H5 high-MMIO bridge window. No real PC is
 * authorized by a matching class code or BAR alone. */
#define H9_AHCI_MMIO_BYTES 4096U
#define H9_AHCI_VIRT_BASE UINT64_C(0xfffffe8001200000)
#define H9_QEMU_PCI_VENDOR 0x8086U
#define H9_QEMU_ICH9_AHCI 0x2922U
/* The fixed 256 MiB Q35 acceptance machine places the ICH9 PCI MMIO BAR5
 * in its low 32-bit PCI aperture. The actual H9 QEMU serial resource
 * inventory observed BAR5=0x810c4000, not the high f0000000 range used by
 * other PC layouts. Only the 16 MiB slice containing this witnessed BAR is
 * accepted; changed firmware allocations require fresh qualification.
 * These bounds are an independent, explicitly isolated
 * QEMU fixture reservation, never inferred from the discovered BAR and
 * never transferable to a real host's PCI memory map. */
#define H9_QEMU_MMIO32_MIN UINT64_C(0x0000000081000000)
#define H9_QEMU_MMIO32_MAX UINT64_C(0x0000000081fff000)
#define H9_QEMU_MMIO32_BYTES UINT64_C(0x01000000)
#define H9_CAP 0x00U
#define H9_PI 0x0cU
#define H9_PORT_BASE 0x100U
#define H9_PORT_STRIDE 0x80U
#define H9_PXSACT 0x34U
#define H9_PXCI 0x38U

static struct nas_x86_qemu_mmio h9_mmio={H9_AHCI_VIRT_BASE,H9_AHCI_MMIO_BYTES};
static struct nas_ahci_io h9_io;
static struct nas_ahci_ro_session h9_session;
static struct nas_port_block h9_block;
static struct nas_pci_mmio_owner h9_mmio_owner;
static struct {
    bool attempted,attached;
    uint8_t bus,slot,function,port;
    struct nas_pci_mmio_claim claim;
} h9_persistent;

/* The built-in Q35 HBA also owns OVMF's ISO/ATAPI port, which is not ours.
 * Observe (never clear/stop) all other SATA ports. An in-flight foreign
 * command forbids taking or releasing our HBA BAR mapping: only the SATA
 * data port's command/FIS engines may be modified by this profile. The
 * firmware may leave an idle ATAPI receive engine FRE/FR enabled, which is
 * not authority to touch its port. */
static int h9_check_other_ports_idle(unsigned excluded) {
    uint32_t cap=0,pi=0;
    int rc=nas_x86_qemu_mmio_read32(&h9_mmio,H9_CAP,&cap);
    if(!rc)rc=nas_x86_qemu_mmio_read32(&h9_mmio,H9_PI,&pi);
    if(rc)return rc;
    const unsigned ports=(cap&31U)+1U;
    /* ICH9 emulates six SATA ports. Do not infer a larger physical HBA
     * from an untrusted BAR snapshot or walk outside one 4 KiB aperture. */
    if(!ports || ports>6U || !pi || (pi>>ports)!=0U)
        return -EOPNOTSUPP;
    for(unsigned p=0;p<ports;p++) {
        if(!(pi&(1U<<p)) || p==excluded)continue;
        const uint32_t offset=H9_PORT_BASE+p*H9_PORT_STRIDE;
        uint32_t ci=0,sact=0;
        rc=nas_x86_qemu_mmio_read32(&h9_mmio,offset+H9_PXCI,&ci);
        if(!rc)rc=nas_x86_qemu_mmio_read32(&h9_mmio,offset+H9_PXSACT,&sact);
        if(rc)return rc;
        if(ci || sact)return -EBUSY;
    }
    return 0;
}

static int64_t port_read(void *backend,uint64_t lba,uint32_t sectors,
                         void *destination) {
    int rc=nas_ahci_ro_read((struct nas_ahci_ro_session *)backend,
                            lba,sectors,destination);
    return rc ? rc : (int64_t)sectors;
}
static const struct nas_port_block_ops h9_ops={.read=port_read};

int nas_x86_h9_ahci_bind(const struct nas_pci_function_info *function,
                         const struct nas_pci_resources *resources) {
    if(!function || !resources)return -EINVAL;
    /* This exact built-in ICH9 controller is required. The full PCI scan
     * has already rejected a second SATA controller before reaching here.
     * fw_cfg then independently establishes that the host is QEMU. */
    if(h9_persistent.attempted ||
       function->bus!=0U || function->slot!=31U || function->function!=2U ||
       function->vendor_id!=H9_QEMU_PCI_VENDOR ||
       function->device_id!=H9_QEMU_ICH9_AHCI ||
       function->class_code!=1U || function->subclass!=6U ||
       function->programming_interface!=1U || !nas_x86_qemu_fw_cfg_signature()) {
        nas_print("[H9 AHCI FAIL] not the isolated QEMU ICH9 SATA controller\n");
        return -ENODEV;
    }
    h9_persistent.attempted=true;
    const struct nas_pci_bar_desc *bar=nas_x86_qemu_find_bar(resources,5U);
    /* A decoded BAR address is not a measured resource size. Qualify the
     * fixed 4 KiB ICH9 QEMU aperture and reserve only that bounded region. */
    if(!bar || bar->kind!=NAS_PCI_BAR_MEM32 || bar->prefetchable ||
       (bar->base&4095U) || bar->base<H9_QEMU_MMIO32_MIN ||
       bar->base>H9_QEMU_MMIO32_MAX) {
        nas_print("[H9 AHCI FAIL] unqualified QEMU BAR5\n");
        return -ENODEV;
    }
    /* This independently fixed Q35 low-MMIO hole is not derived from the
     * observed BAR. A BAR snapshot cannot prove its own host-bridge range. */
    struct nas_pci_mmio_claim claim={0};
    uint64_t saved_flags=0;
    int rc=nas_x86_qemu_map_bar(&h9_mmio_owner,resources,bar,H9_AHCI_VIRT_BASE,
                                H9_AHCI_MMIO_BYTES,H9_QEMU_MMIO32_MIN,
                                H9_QEMU_MMIO32_BYTES,&claim,&saved_flags);
    if(rc)return rc;
    nas_print("[H9 AHCI BAR QUALIFIED] bus=0 slot=31 function=2 BAR5=");
    nas_hex(bar->base);
    nas_print(" 4096-byte QEMU-only UC/NX mapping\n");

    /* All mapped port state is read-only until full QEMU qualification.
     * Do not run SATA commands while firmware-owned ATAPI DMA is in flight. */
    rc=h9_check_other_ports_idle(32U);
    if(rc) {
        nas_print("[H9 AHCI FAIL] foreign/in-flight SATA port commands\n");
        nas_x86_qemu_restore_if(saved_flags);
        return rc;
    }

    /* Preserve only PCI Command bits. Never echo Status's W1C bits into
     * configuration DWORD 0x04 while enabling MEM and bus mastering. */
    const uint32_t original=(uint32_t)resources->command;
    if((original&UINT32_C(6))!=UINT32_C(6))
        nas_x86_qemu_pci_command_write(function->bus,function->slot,function->function,original|UINT32_C(6));
    h9_io=(struct nas_ahci_io){
        .context=&h9_mmio,.mmio_bytes=H9_AHCI_MMIO_BYTES,
        .read32=nas_x86_qemu_mmio_read32,.write32=nas_x86_qemu_mmio_write32
    };
    rc=nas_ahci_ro_open(&h9_io,&nas_dma_qemu_coherent,&h9_session);
    if(!rc && (h9_session.sector_bytes!=512U ||
               h9_session.sectors<16384U))rc=-EOPNOTSUPP;
    if(!rc)rc=nas_port_block_attach(&h9_block,"ahci0",&h9_session,
                                    h9_session.sectors,h9_session.sector_bytes,
                                    8U,true,NAS_PORT_BLOCK_SECTORS,&h9_ops);
    if(rc) {
        /* No media is a hard negative case, not a rescue boot. A failure
         * in quiescence must leave DMA and BAR pinned with bus master on. */
        if(h9_session.io)
            (void)nas_ahci_ro_close(&h9_session);
        if(!h9_session.may_dma && !h9_session.io) {
            /* The ICH9 HBA also owns the firmware CDROM's ATAPI port.
             * Do not turn off controller-wide MEM/BM decode after touching
             * only our SATA port: unrelated port DMA may still be active. */
            (void)nas_pci_mmio_quiesced_unmap_release(
                &h9_mmio_owner,&claim,H9_AHCI_VIRT_BASE,true,
                nas_arch_x86_unmap_device_pages);
        }
        nas_x86_qemu_restore_if(saved_flags);
        nas_print("[H9 AHCI FAIL] required QEMU SATA disk unavailable\n");
        return rc;
    }
    h9_persistent.claim=claim;
    h9_persistent.bus=function->bus;
    h9_persistent.slot=function->slot;
    h9_persistent.function=function->function;
    h9_persistent.port=h9_session.port;
    h9_persistent.attached=true;
    nas_x86_qemu_restore_if(saved_flags);
    nas_print("[H9 AHCI BLOCK ATTACHED] name=ahci0 read-only sectors=");
    nas_dec((unsigned)h9_session.sectors);
    nas_print(" sector_bytes=512; no ATA write commands\n");
    return 0;
}

int nas_x86_h9_ahci_shutdown(void) {
    if(!h9_persistent.attached)return -ENODEV;
    int rc=nas_port_block_detach(&h9_block);
    if(rc)return rc; /* Filesystem or user still owns a block reference. */
    rc=nas_ahci_ro_close(&h9_session);
    if(rc)return rc; /* PxCMD.CR/FR still active: keep live DMA pinned. */
    rc=h9_check_other_ports_idle(h9_persistent.port);
    if(rc)return rc; /* Foreign ATAPI DMA: never declare whole BAR idle. */
    /* AHCI's BAR5 and PCI Command are shared with firmware's ATAPI CDROM
     * port. Our SATA port has stopped; preserving global MEM/BM is safer
     * than disabling an unrelated, potentially still-running DMA engine. */
    rc=nas_pci_mmio_quiesced_unmap_release(
        &h9_mmio_owner,&h9_persistent.claim,H9_AHCI_VIRT_BASE,true,
        nas_arch_x86_unmap_device_pages);
    if(rc)return rc;
    h9_persistent.attached=false;
    nas_print("[H9 AHCI QUIESCED RELEASED] SATA port CR/FR=0 DMA pages freed BAR5 UC unmapped PCI claim released\n");
    return 0;
}
