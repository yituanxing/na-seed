#ifndef NASEED_X86_H15_FTDI_H
#define NASEED_X86_H15_FTDI_H

#include "../drivers/core/pci_resources.h"
#include <stdbool.h>
#include <stdint.h>
#include "../drivers/usb/serial_backend.h"

/* One QEMU-qualified PCI xHCI and one real 0403:6001 FTDI USB-serial
 * function. Physical USB bulk endpoints never alias the bootstrap UART. */
int nas_x86_h15_ftdi_bind(const struct nas_pci_function_info *,
                          const struct nas_pci_resources *);
/* Scheduler-deferred physical USB Bulk-IN pump; never accesses user memory. */
int nas_x86_h15_ftdi_poll(unsigned budget);
unsigned nas_x86_h15_ftdi_rx_count(void);
int nas_x86_h15_ftdi_read(uint8_t *dst,uint32_t bytes);
int nas_x86_h15_ftdi_write(const uint8_t *src,uint32_t bytes);
void nas_x86_h15_ftdi_rx_flush(void);
/* Borrowed physical class port, never used directly by filesystem code. */
const struct nas_usb_serial_backend *nas_x86_h15_ftdi_backend(void);
int nas_x86_h15_ftdi_shutdown(void);

#endif
