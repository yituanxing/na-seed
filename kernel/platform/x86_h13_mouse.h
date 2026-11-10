#ifndef NASEED_X86_H13_USB_MOUSE_H
#define NASEED_X86_H13_USB_MOUSE_H

#include "../drivers/core/pci_resources.h"

/* One explicit QEMU q35 xHCI / USB HID Boot Mouse acceptance session. */
int nas_x86_h13_mouse_bind(const struct nas_pci_function_info *,
                          const struct nas_pci_resources *);
int nas_x86_h13_mouse_publish_input(void);
int nas_x86_h13_mouse_poll(unsigned budget);
int nas_x86_h13_mouse_shutdown(void);

#endif
