/* H15 Linux USB serial character interface. Never a console TTY alias:
 * reads/writes cross real QEMU FT232BM xHCI Bulk-IN/Bulk-OUT endpoints. */
#include "usb_serial_chrdev.h"
#include "../drivers/core/chrdev.h"
#include "../drivers/tty.h"
#include "../drivers/serial.h"
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include <stdbool.h>
#include <stdint.h>

#define H15_LINUX_B115200 UINT32_C(0x1002)
#define H15_LINUX_CBAUD   UINT32_C(0x100f)
#define H15_LINUX_CSIZE   UINT32_C(0x0030)
#define H15_LINUX_CS8     UINT32_C(0x0030)
#define H15_LINUX_CSTOPB  UINT32_C(0x0040)
#define H15_LINUX_PARENB  UINT32_C(0x0100)

static bool published;
static const struct nas_usb_serial_backend *serial_backend;
static bool backend_live(void) {
    return published && serial_backend &&
           serial_backend->ready(serial_backend->context);
}
static struct nas_termios termios;
static struct nas_chrdev_node h15_node;
static int h15_open(unsigned slot,unsigned unit) {
    if(slot>=NAS_OPEN_FILE_LIMIT || unit!=0U)return -EINVAL;
    if(!backend_live())return -ENODEV;
    return 0;
}
static const struct nas_chrdev_provider h15_provider={
    .name="qemu-ft232bm-usb-serial",.open=h15_open
};
int nas_usb_serial_chrdev_publish(const struct nas_usb_serial_backend *device) {
    if(published)return -EBUSY;
    if(!device || !device->ready || !device->publication_pin ||
       !device->publication_unpin || !device->rx_available ||
       !device->flush_rx || !device->read || !device->write)return -EINVAL;
    if(!device->ready(device->context))return -ENODEV;
    int pinned=device->publication_pin(device->context);
    if(pinned)return pinned;
    /* FT232BM is fixed by the real USB vendor request to 115200 8N1;
     * only the c_cflag differs from the shared Linux N_TTY defaults. */
    nas_termios_defaults(&termios,
        H15_LINUX_B115200|H15_LINUX_CS8|0000200U|0004000U);
    h15_node=(struct nas_chrdev_node){
        .path="/dev/ttyUSB0",
        .info={.fd_kind=FD_USB_SERIAL,.unit=0U,.dev=2U,
               .ino=4115U,.rdev=188U<<8,.mode=0020666U},
        .provider=&h15_provider
    };
    int rc=nas_chrdev_node_register(&h15_node);
    if(rc) {
        device->publication_unpin(device->context);
        return rc;
    }
    serial_backend=device;
    published=true;
    nas_print("[H15 USB SERIAL ATTACHED] /dev/ttyUSB0 FT232BM BUS_USB\n");
    return 0;
}
int nas_usb_serial_chrdev_unpublish(void) {
    if(!published)return -ENODEV;
    /* Registry tracks one pin per shared File. Return EBUSY until original
     * BusyBox stty/head/printf close their real /dev/ttyUSB0 descriptors. */
    int rc=nas_chrdev_node_unregister(&h15_node);
    if(!rc) {
        published=false;
        serial_backend->publication_unpin(serial_backend->context);
        serial_backend=NULL;
    }
    return rc;
}
uint64_t nas_usb_serial_chrdev_read_plan(unsigned slot,uint64_t requested) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !backend_live())return (uint64_t)-ENODEV;
    if(!requested)return 0;
    return serial_backend->rx_available(serial_backend->context) ?
        requested:(uint64_t)-EAGAIN;
}
uint64_t nas_usb_serial_chrdev_read(unsigned slot,uint8_t *dst,uint64_t n) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !backend_live())return (uint64_t)-ENODEV;
    if(n>1024U)n=1024U; /* single kernel-owned FS read buffer */
    return (uint64_t)(int64_t)serial_backend->read(serial_backend->context,
                                                   dst,(uint32_t)n);
}
uint64_t nas_usb_serial_chrdev_write(unsigned slot,const uint8_t *src,
                                     uint64_t n) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !backend_live())return (uint64_t)-ENODEV;
    if(n>1024U)n=1024U;
    return (uint64_t)(int64_t)serial_backend->write(serial_backend->context,
                                                    src,(uint32_t)n);
}
uint16_t nas_usb_serial_chrdev_poll(unsigned slot,uint16_t events) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !backend_live())return 0x0018U; /* POLLERR|POLLHUP */
    uint16_t result=events&0x0004U; /* POLLOUT */
    if(serial_backend->rx_available(serial_backend->context))
        result|=events&0x0001U; /* POLLIN */
    return result;
}
void nas_usb_serial_chrdev_release(unsigned slot) {
    /* No per-open state is allocated; chrdev-node File pins are released
     * once by the existing OpenFile final-close path. */
    (void)slot;
}
uint64_t nas_usb_serial_chrdev_ioctl(uint64_t fd,uint64_t cmd,uint64_t arg) {
    if(fd>=NAS_FD_LIMIT || !nas_current_task ||
       nas_current_task->fds->entries[fd].kind!=FD_USB_SERIAL)
        return (uint64_t)-EBADF;
    if(!backend_live())return (uint64_t)-ENODEV;
    switch((uint32_t)cmd) {
    case 0x5401U: /* TCGETS: exact 36-byte native Linux termios ABI */
        return nas_user_write_bytes(arg,(const uint8_t *)&termios,
                                    sizeof(termios))?0:(uint64_t)-EFAULT;
    case 0x5402U: /* TCSETS */
    case 0x5403U: /* TCSETSW */
    case 0x5404U: { /* TCSETSF */
        struct nas_termios proposed;
        if(!nas_user_read_bytes(arg,(uint8_t *)&proposed,sizeof(proposed)))
            return (uint64_t)-EFAULT;
        /* QEMU vendor requests qualified a fixed actual FTDI 115200 8N1
         * link. Do not report successful hardware mode changes that never
         * reached a USB control endpoint. BusyBox stty raw -echo 115200
         * retains this exact c_cflag mode. */
        if(proposed.line ||
           (proposed.cflag&H15_LINUX_CBAUD)!=H15_LINUX_B115200 ||
           (proposed.cflag&H15_LINUX_CSIZE)!=H15_LINUX_CS8 ||
           (proposed.cflag&(H15_LINUX_CSTOPB|H15_LINUX_PARENB)))
            return (uint64_t)-EINVAL;
        termios=proposed;
        if((uint32_t)cmd==0x5404U)
            serial_backend->flush_rx(serial_backend->context);
        return 0;
    }
    case 0x541bU: { /* FIONREAD/TIOCINQ: actual retained USB RX bytes */
        uint32_t pending=serial_backend->rx_available(serial_backend->context);
        return nas_user_write_bytes(arg,(const uint8_t *)&pending,
                                    sizeof(pending))?0:(uint64_t)-EFAULT;
    }
    case 0x5413U: { /* TIOCGWINSZ: a serial port has no physical screen */
        const uint16_t winsize[4]={0,0,0,0};
        return nas_user_write_bytes(arg,(const uint8_t *)winsize,
                                    sizeof(winsize))?0:(uint64_t)-EFAULT;
    }
    case 0x540bU: /* TCFLSH; input side only has a bounded RX FIFO */
        if(arg>2U)return (uint64_t)-EINVAL;
        if(arg==0U || arg==2U)
            serial_backend->flush_rx(serial_backend->context);
        return 0;
    case 0x5424U: { /* TIOCGETD: N_TTY discipline */
        uint32_t line=0U;
        return nas_user_write_bytes(arg,(const uint8_t *)&line,
                                    sizeof(line))?0:(uint64_t)-EFAULT;
    }
    default:return (uint64_t)-ENOTTY;
    }
}
