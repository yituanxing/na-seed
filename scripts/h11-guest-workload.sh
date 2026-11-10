#!/bin/sh
# Run by original signed dynamic Alpine BusyBox loaded ONLY from a genuine
# QEMU xHCI -> USB Mass Storage BOT/SCSI readonly ext4 `/` disk.
set -eu

test "$(/bin/busybox cat /root/h11-disk-only.txt)" = \
    'H11_USB_SIGNED_ALPINE_DISK_ONLY_20261110'
/bin/busybox cat /root/h11-disk-only.txt
/bin/busybox cmp -s /root/h11-disk-only.txt /root/h11-copy.txt
/bin/busybox echo H11_ALPINE_BUSYBOX_USB_PASS

/usr/bin/lua5.4 -e 'assert(21 * 2 == 42); print("H11_ALPINE_LUA54_USB_PASS")'
/usr/bin/jq -nr '"H11_ALPINE_JQ_USB_PASS"'
/usr/bin/sqlite3 :memory: "SELECT 'H11_ALPINE_SQLITE_USB_PASS';"

if /bin/busybox mkdir /root/h11-unexpected-write; then
    /bin/busybox echo H11_ALPINE_READONLY_VIOLATION
    exit 1
fi
test ! -e /root/h11-unexpected-write
/bin/busybox echo H11_ALPINE_READONLY_USB_PASS
/bin/busybox echo H11_ORIGINAL_ALPINE_DYNAMIC_ELFS_USB_PASS
