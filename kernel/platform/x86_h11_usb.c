#include "x86_h11_usb.h"
#include "x86_qemu_xhci.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/usb/xhci.h"
#include "../drivers/usb/msc.h"
#include "../drivers/port/block.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdbool.h>
#include <stdint.h>

/* H11 accepts exactly QEMU's qemu-xhci (PCI 1b36:000d at 00:04.0),
 * not an arbitrary USB controller with the matching xHCI class code.
 * QEMU registers BAR0+BAR1 as a 64-bit, nonprefetchable MEM aperture;
 * this is the fixed 16 KiB QEMU implementation, NOT a PCI size probe.
 * The host's Q35 64-bit PCI memory hole begins at 0xC000000000,
 * independently observed in the H11 genuine QEMU/OVMF PCI inventory.
 * The narrow fixed 16MiB high acceptance window is independent of the BAR.
 */
#define H11_XHCI_VIRT_BASE UINT64_C(0xfffffe8001600000)

static struct nas_x86_qemu_xhci h11_hw={.mmio={H11_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES}};
static struct nas_xhci_ro_session h11_xhci;
static struct nas_usb_msc_ro_session h11_msc;
static struct nas_port_block h11_block;
static struct {
    bool attached;
} h11_persistent;

static int64_t h11_block_read(void *backend,uint64_t lba,uint32_t sectors,
                              void *destination) {
    int rc=nas_usb_msc_ro_read((struct nas_usb_msc_ro_session *)backend,
                               lba,sectors,destination);
    return rc ? rc : (int64_t)sectors;
}
static const struct nas_port_block_ops h11_block_ops={.read=h11_block_read};

int nas_x86_h11_usb_bind(const struct nas_pci_function_info *fn,
                         const struct nas_pci_resources *resources) {
    uint64_t flags=0;
    int rc=nas_x86_qemu_xhci_prepare(&h11_hw,fn,resources,&flags,
        "[H11 XHCI FAIL] unqualified QEMU xHCI function\n",
        "[H11 XHCI FAIL] unqualified 64-bit BAR0 (fixed 16KiB QEMU aperture)\n",
        "[H11 XHCI BAR QUALIFIED] QEMU 00:04.0 1b36:000d BAR0 MEM64="," 16KiB UC/NX no IRQ\n");
    if(rc)return rc;
    rc=nas_xhci_ro_open(&h11_hw.io,&nas_dma_qemu_coherent,&h11_xhci);
    if(!rc)rc=nas_usb_msc_ro_open(&h11_xhci,&h11_msc);
    if(!rc && (h11_msc.sector_bytes!=512U || h11_msc.sectors<16384U))
        rc=-EOPNOTSUPP;
    if(!rc)rc=nas_port_block_attach(&h11_block,"usb0",&h11_msc,
                                   h11_msc.sectors,h11_msc.sector_bytes,
                                   8U,true,NAS_PORT_BLOCK_SECTORS,
                                   &h11_block_ops);
    if(rc) {
        nas_print("[H11 XHCI FAIL DIAG] stage=");
        nas_dec((unsigned)h11_xhci.debug_stage);
        nas_print(" rc=");
        nas_dec((unsigned)(rc < 0 ? -rc : rc));
        nas_print(" cc=");
        nas_dec((unsigned)h11_xhci.debug_completion_code);
        nas_print(" event=");
        nas_dec((unsigned)h11_xhci.debug_event_type);
        nas_print(" portsc=");
        nas_hex(h11_xhci.debug_portsc);
        nas_print("\n");
        /* Open can fail after xHCI has armed DMA. Refuse to release BAR
         * unless the controller confirms HCHalted and frees all 7 leases.
         * The block backend never performs SCSI WRITE / media flush. */
        (void)nas_usb_msc_ro_close(&h11_msc);
        if(nas_xhci_ro_close(&h11_xhci)) {
            nas_print("[H11 XHCI PINNED] controller stop unproven, retain DMA and BAR\n");
        } else {
            (void)nas_x86_qemu_xhci_release(&h11_hw);
        }
        nas_x86_qemu_restore_if(flags);
        nas_print("[H11 USB FAIL] required QEMU USB Mass Storage unavailable\n");
        return rc;
    }
    h11_persistent.attached=true;
    nas_x86_qemu_restore_if(flags);
    nas_print("[H11 USB BLOCK ATTACHED] name=usb0 read-only sectors=");
    nas_dec((unsigned)h11_msc.sectors);
    nas_print(" sector_bytes=512 xHCI/BOT/SCSI READ10\n");
    return 0;
}

int nas_x86_h11_usb_shutdown(void) {
    if(!h11_persistent.attached)return -ENODEV;
    int rc=nas_port_block_detach(&h11_block);
    if(rc)return rc; /* No filesystem or open File may own the device. */
    rc=nas_usb_msc_ro_close(&h11_msc);
    if(rc)return rc;
    rc=nas_xhci_ro_close(&h11_xhci);
    if(rc)return rc; /* Crucial: no BAR release if controller still DMA. */
    rc=nas_x86_qemu_xhci_release(&h11_hw);
    if(rc)return rc;
    h11_persistent.attached=false;
    nas_print("[H11 USB QUIESCED RELEASED] USB SCSI disk detached, xHC halted, 7 DMA pages freed, 4 BAR0 UC pages unmapped\n");
    return 0;
}
