#ifndef NASEED_USB_MSC_READONLY_H
#define NASEED_USB_MSC_READONLY_H

#include <stdbool.h>
#include <stdint.h>

struct nas_xhci_ro_session;

/* QEMU-only USB Mass Storage, interface 08/06/50, BOT LUN 0 and SBC disk.
 * The xHCI layer supplies a fully enumerated/configured interface with
 * synchronous, exact-byte bulk endpoint transfers. This is the USB/SCSI
 * read-only device boundary; it does not write, format, flush or reset media.
 * Hold this lifetime-stable session until nas_port_block_detach succeeds. */
struct nas_usb_msc_ro_session {
    struct nas_xhci_ro_session *xhci;
    uint64_t sectors;
    uint32_t sector_bytes;
    uint32_t next_tag;
    bool ready;
    bool in_io;
};

int nas_usb_msc_ro_open(struct nas_xhci_ro_session *,
                        struct nas_usb_msc_ro_session *);
int nas_usb_msc_ro_read(struct nas_usb_msc_ro_session *, uint64_t lba,
                        uint32_t sectors, void *kernel_dst);
int nas_usb_msc_ro_close(struct nas_usb_msc_ro_session *);

#endif
