#include "x86_h13_mouse.h"
#include "x86_qemu_hid_input.h"
#include "../arch/x86_64/mmu.h"
#include "../drivers/serial.h"
#include "../errno.h"
#include <stdint.h>

/* Standalone QEMU xHCI profile; device lifecycle is shared, not milestone-
 * specific. Only diagnostic contracts and class protocol are profile data. */
#define H13_XHCI_VIRT_BASE UINT64_C(0xfffffe8001800000)
static struct nas_x86_qemu_hid_port h13={
    .hw={.mmio={H13_XHCI_VIRT_BASE,NAS_H11_XHCI_MMIO_BYTES}}
};
static const struct nas_x86_qemu_hid_profile h13_profile={
    .mouse=true,
    .function_failure="[H13 MOUSE FAIL] unqualified QEMU PCI xHCI\n",
    .bar_failure="[H13 MOUSE FAIL] unqualified MEM64 BAR0; 16MiB Q35 high whitelist only\n",
    .bar_label="[H13 MOUSE BAR QUALIFIED] QEMU 00:04.0 1b36:000d MEM64=",
    .bar_suffix=" 16KiB UC/NX\n",
    .diagnostic="[H13 MOUSE FAIL DIAG] stage=",
    .pinned="[H13 MOUSE PINNED] xHC halt unproven: DMA/BAR retained\n",
    .missing="[H13 MOUSE FAIL] required QEMU USB Boot Mouse unavailable\n",
    .enumerated="[H13 MOUSE USB ENUMERATED] 4-byte Boot Mouse interrupt-IN; input class publication deferred\n",
    .attached="[H13 MOUSE INPUT ATTACHED] /dev/input/event0 QEMU USB HID Boot Mouse BUS_USB EV_REL/EV_KEY/SYN_REPORT\n",
    .released="[H13 MOUSE QUIESCED RELEASED] event0 retired, xHC halted, seven DMA pages freed, 4 BAR0 UC pages unmapped\n",
    .rejected="[H13 MOUSE REPORT REJECTED] malformed 4-byte Boot Mouse payload\n"
};
int nas_x86_h13_mouse_bind(const struct nas_pci_function_info *fn,
             const struct nas_pci_resources *resources) {
    return nas_x86_qemu_hid_port_bind(&h13,&h13_profile,fn,resources);
}
int nas_x86_h13_mouse_publish_input(void) {
    return nas_x86_qemu_hid_port_publish(&h13,&h13_profile);
}
int nas_x86_h13_mouse_poll(unsigned budget) {
    return nas_x86_qemu_hid_port_poll(&h13,&h13_profile,budget);
}
int nas_x86_h13_mouse_shutdown(void) {
    return nas_x86_qemu_hid_port_shutdown(&h13,&h13_profile);
}
