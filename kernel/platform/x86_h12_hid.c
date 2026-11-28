#include "x86_h12_hid.h"
#include "x86_qemu_hid_input.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdint.h>

/* Standalone QEMU xHCI profile; device lifecycle is shared, not milestone-
 * specific. Only diagnostic contracts and class protocol are profile data. */
#define H12_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)
static struct nas_x86_qemu_hid_port h12={
    .hw={.mmio={H12_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES}}
};
static const struct nas_x86_qemu_hid_profile h12_profile={
    .mouse=false,
    .function_failure="[H12 HID FAIL] unqualified QEMU PCI xHCI\n",
    .bar_failure="[H12 HID FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n",
    .bar_label="[H12 HID BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64=",
    .bar_suffix=" 16KiB UC/NX\n",
    .diagnostic="[H12 HID FAIL DIAG] stage=",
    .pinned="[H12 HID PINNED] xHC halt unproven: DMA/BAR retained\n",
    .missing="[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable\n",
    .enumerated="[H12 HID USB ENUMERATED] 8-byte Boot Keyboard interrupt-IN; input class publication deferred\n",
    .attached="[H12 HID INPUT ATTACHED] /dev/input/event0 QEMU USB Boot Keyboard EV_KEY/SYN_REPORT\n",
    .released="[H12 HID QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n",
    .rejected="[H12 HID REPORT REJECTED] malformed Boot Keyboard payload\n"
};
int nas_x86_h12_hid_bind(const struct nas_pci_function_info *fn,
             const struct nas_pci_resources *resources) {
    return nas_x86_qemu_hid_port_bind(&h12,&h12_profile,fn,resources);
}
int nas_x86_h12_hid_publish_input(void) {
    return nas_x86_qemu_hid_port_publish(&h12,&h12_profile);
}
int nas_x86_h12_hid_poll(unsigned budget) {
    return nas_x86_qemu_hid_port_poll(&h12,&h12_profile,budget);
}
int nas_x86_h12_hid_shutdown(void) {
    return nas_x86_qemu_hid_port_shutdown(&h12,&h12_profile);
}
