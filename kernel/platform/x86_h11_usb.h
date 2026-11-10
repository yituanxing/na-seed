#ifndef NASEED_X86_H11_USB_H
#define NASEED_X86_H11_USB_H

#include "../drivers/core/pci_resources.h"

/* H11 is an opt-in, strictly isolated QEMU q35 xHCI 1b36:000d at 00:04.0
 * USB Mass Storage root. No firmware class code authorizes physical USB DMA. */
int nas_x86_h11_usb_bind(const struct nas_pci_function_info *,
                         const struct nas_pci_resources *);
int nas_x86_h11_usb_shutdown(void);

#endif
