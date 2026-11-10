#ifndef NASEED_X86_H14_DUAL_HID_H
#define NASEED_X86_H14_DUAL_HID_H

#include "../drivers/core/pci_resources.h"

/* H14 QEMU-only: exact one xHCI controller, two physical USB root-port
 * devices at once, 8-byte Boot Keyboard and 4-byte Boot Mouse.  No hub,
 * VirtIO input or synthetic user event path; original Alpine evtest only. */
int nas_x86_h14_dual_hid_bind(const struct nas_pci_function_info *,
                             const struct nas_pci_resources *);
int nas_x86_h14_dual_hid_publish_input(void);
int nas_x86_h14_dual_hid_poll(unsigned budget);
int nas_x86_h14_dual_hid_shutdown(void);

#endif
