#!/bin/sh
# H15: REAL QEMU FT232BM USB-serial -> /dev/ttyUSB0. This script runs on
# the immutable signed-original-Alpine NVMe root, not a rescue executable.
# All actual serial reads/writes are performed by the original signed Alpine
# dynamic BusyBox binary. The host-only oracle supplies an unpredictable nonce.
set -eu

test -c /dev/ttyUSB0 || {
    printf '%s\n' H15_FTDI_GUEST_FAIL_MISSING_TTYUSB0
    exit 1
}
test -x /bin/busybox
test "$(/bin/busybox cat /root/h15-disk-only.txt)" = \
    'H15_FTDI_SIGNED_ALPINE_DISK_ONLY_20261110'
/bin/busybox cmp -s /root/h15-disk-only.txt /root/h15-copy.txt
/bin/busybox echo H15_ALPINE_BUSYBOX_NVME_PASS
/usr/bin/lua5.4 -e 'assert(21 * 2 == 42); print("H15_ALPINE_LUA54_NVME_PASS")'
/usr/bin/jq -nr '"H15_ALPINE_JQ_NVME_PASS"'
/usr/bin/sqlite3 :memory: "SELECT 'H15_ALPINE_SQLITE_NVME_PASS';"
if /bin/busybox mkdir /root/h15-unexpected-write; then
    /bin/busybox echo H15_ALPINE_READONLY_VIOLATION
    exit 1
fi
test ! -e /root/h15-unexpected-write
/bin/busybox echo H15_ALPINE_READONLY_NVME_PASS
/bin/busybox echo H15_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS

# Exercise real Linux termios IOCTLs on the genuine USB tty. This MUST
# fail rather than falling back to /dev/ttyS0 or the rescue serial console.
if ! /bin/busybox stty -F /dev/ttyUSB0 raw -echo 115200; then
    printf '%s\n' H15_FTDI_GUEST_FAIL_STTY
    exit 1
fi
printf '%s\n' H15_FTDI_ORIGINAL_BUSYBOX_STTY_PASS
printf '%s\n' H15_USB_SERIAL_READY_FOR_HOST

# The original signed BusyBox head reads through real FTDI BULK IN; no
# nonce bytes are present in the signed media or in this guest script.
h15_nonce=$(/bin/busybox head -c 32 /dev/ttyUSB0) || {
    printf '%s\n' H15_FTDI_GUEST_FAIL_HEAD
    exit 1
}
h15_bytes=$(printf '%s' "$h15_nonce" | /bin/busybox wc -c)
test "$h15_bytes" -eq 32 || {
    printf '%s\n' H15_FTDI_GUEST_FAIL_SHORT_NONCE
    exit 1
}
# Ensure only the host's unpredictable printable hex challenge is echoed.
case "$h15_nonce" in
    *[!0123456789abcdef]*)
        printf '%s\n' H15_FTDI_GUEST_FAIL_INVALID_NONCE
        exit 1 ;;
esac
# Original signed BusyBox printf writes the exact same unpredictable bytes
# back to the independent host socket via real FTDI BULK OUT.
if ! /bin/busybox printf '%s' "$h15_nonce" >/dev/ttyUSB0; then
    printf '%s\n' H15_FTDI_GUEST_FAIL_PRINTF
    exit 1
fi
printf '%s\n' H15_ORIGINAL_ALPINE_BUSYBOX_USB_SERIAL_NONCE_RETURNED
