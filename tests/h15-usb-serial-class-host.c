/* Production USB serial FileOps class + chrdev registry, fake physical port.
 * HOST TEST ONLY. Genuine QEMU FTDI/Alpine host nonce remains mandatory. */
#include "../kernel/fs/usb_serial_chrdev.h"
#include "../kernel/drivers/core/chrdev.h"
#include "../kernel/task.h"
#include "../kernel/mm/usercopy.h"
#include "../kernel/errno.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "[H15 CLASS FAIL] line %u: %s\n", \
            (unsigned)__LINE__, #x); return 1; \
} } while (0)

struct nas_task *nas_current_task;
void nas_print(const char *s) { (void)s; }
bool nas_user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len) {
    if (!addr || !dst) return false;
    memcpy(dst, (const void *)(uintptr_t)addr, (size_t)len);
    return true;
}
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len) {
    if (!addr || !src) return false;
    memcpy((void *)(uintptr_t)addr, src, (size_t)len);
    return true;
}

struct fake_port {
    bool ready;
    unsigned pins, available, reads, writes, flushes;
};
static bool port_ready(void *ctx) {
    return ((struct fake_port *)ctx)->ready;
}
static int port_pin(void *ctx) {
    struct fake_port *p = ctx;
    if (!p->ready) return -ENODEV;
    if (p->pins) return -EBUSY;
    ++p->pins;
    return 0;
}
static void port_unpin(void *ctx) {
    struct fake_port *p = ctx;
    if (p->pins) --p->pins;
}
static unsigned port_available(void *ctx) {
    return ((struct fake_port *)ctx)->available;
}
static void port_flush(void *ctx) {
    struct fake_port *p = ctx;
    p->available = 0;
    ++p->flushes;
}
static int port_read(void *ctx, uint8_t *data, uint32_t bytes) {
    struct fake_port *p = ctx;
    if (!bytes) return 0;
    if (!p->available) return -EAGAIN;
    data[0] = 'R';
    --p->available;
    ++p->reads;
    return 1;
}
static int port_write(void *ctx, const uint8_t *data, uint32_t bytes) {
    struct fake_port *p = ctx;
    if (!data && bytes) return -EINVAL;
    p->writes += bytes;
    return (int)bytes;
}
static struct nas_usb_serial_backend port_backend(struct fake_port *p) {
    return (struct nas_usb_serial_backend){
        .context=p, .ready=port_ready, .publication_pin=port_pin,
        .publication_unpin=port_unpin, .rx_available=port_available,
        .flush_rx=port_flush, .read=port_read, .write=port_write
    };
}
int main(void) {
    struct fake_port physical = {0};
    struct nas_usb_serial_backend port = port_backend(&physical);
    CHECK(nas_usb_serial_chrdev_publish(NULL) == -EINVAL);
    CHECK(nas_usb_serial_chrdev_publish(&port) == -ENODEV);
    CHECK(!physical.pins && !nas_chrdev_node_at(0));

    physical.ready = true;
    CHECK(nas_usb_serial_chrdev_publish(&port) == 0);
    CHECK(physical.pins == 1U);
    CHECK(nas_usb_serial_chrdev_publish(&port) == -EBUSY);
    struct nas_chrdev_node *node = nas_chrdev_node_at(0);
    CHECK(node && !strcmp(node->path, "/dev/ttyUSB0"));
    struct nas_chrdev_info info = {0};
    const struct nas_chrdev_provider *provider = NULL;
    struct nas_chrdev_node *found = NULL;
    CHECK(nas_chrdev_describe("/dev/ttyUSB0", &info, &provider, &found) == 0);
    CHECK(found == node && provider && provider->open(0U, 0U) == 0);

    CHECK(nas_usb_serial_chrdev_read_plan(0U, 5U) == (uint64_t)-EAGAIN);
    CHECK(nas_usb_serial_chrdev_poll(0U, 5U) == 4U);
    physical.available = 3U;
    CHECK(nas_usb_serial_chrdev_read_plan(0U, 5U) == 5U);
    CHECK(nas_usb_serial_chrdev_poll(0U, 5U) == 5U);
    uint8_t buf[4] = {0};
    CHECK(nas_usb_serial_chrdev_read(0U, buf, 4U) == 1U && buf[0] == 'R');
    CHECK(physical.reads == 1U && physical.available == 2U);
    CHECK(nas_usb_serial_chrdev_write(0U, buf, 3U) == 3U);
    CHECK(physical.writes == 3U);

    struct nas_task task = {0};
    struct nas_files files = {0};
    task.fds = &files.table;
    nas_current_task = &task;
    files.table.entries[3].kind = FD_USB_SERIAL;
    uint32_t pending = 0;
    CHECK(nas_usb_serial_chrdev_ioctl(3U, 0x541bU,
          (uint64_t)(uintptr_t)&pending) == 0U && pending == 2U);
    CHECK(nas_usb_serial_chrdev_ioctl(3U, 0x540bU, 0U) == 0U);
    CHECK(!physical.available && physical.flushes == 1U);

    CHECK(nas_chrdev_node_pin(node) == 0);
    CHECK(nas_usb_serial_chrdev_unpublish() == -EBUSY);
    CHECK(physical.pins == 1U && node->registered);
    nas_chrdev_node_put(node);
    CHECK(nas_usb_serial_chrdev_unpublish() == 0);
    CHECK(!physical.pins && !nas_chrdev_node_at(0));
    CHECK(nas_usb_serial_chrdev_read_plan(0U, 1U) == (uint64_t)-ENODEV);

    CHECK(nas_usb_serial_chrdev_publish(&port) == 0);
    physical.ready = false; /* Disconnect: refuse I/O without aliasing UART. */
    CHECK(nas_usb_serial_chrdev_read(0U, buf, 1U) == (uint64_t)-ENODEV);
    CHECK(nas_usb_serial_chrdev_unpublish() == 0 && !physical.pins);

    puts("[H15 CLASS HOST PASS] FileOps publication, owner pin, busy unbind, offline guard");
    return 0;
}
