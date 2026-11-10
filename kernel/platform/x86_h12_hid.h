#ifndef NASEED_X86_H12_USB_HID_H
#define NASEED_X86_H12_USB_HID_H

#include "../drivers/core/pci_resources.h"

/* H12: isolated QEMU xHCI + one USB Boot Protocol HID keyboard.
 * The PCI inventory must qualify a unique controller before DMA opens. */
int nas_x86_h12_hid_bind(const struct nas_pci_function_info *,
                         const struct nas_pci_resources *);
/* Called after nas_input_chrdev_init() installed the eventN publisher. */
int nas_x86_h12_hid_publish_input(void);
int nas_x86_h12_hid_poll(unsigned budget);
int nas_x86_h12_hid_shutdown(void);

#endif
