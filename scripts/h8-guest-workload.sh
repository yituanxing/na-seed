#!/bin/sh
# H8 runs from an original, signature-verified Alpine BusyBox loaded from
# QEMU's read-only NVMe ext4 ROOT (not from the Limine rescue CPIO).
set -eu

# The disk-only marker and this script must both come from the selected ext4
# root.  None of these paths is present in the rescue CPIO.
test "$(/bin/busybox cat /root/h8-disk-only.txt)" = \
    'H8_NVME_SIGNED_ALPINE_DISK_ONLY_20261110'
/bin/busybox cmp -s /root/h8-disk-only.txt /root/h8-copy.txt
/bin/busybox echo H8_ALPINE_BUSYBOX_NVME_PASS

# These are independently packaged, original Alpine dynamically linked
# executables, NOT BusyBox applets and NOT host-compiled guest stand-ins.
/usr/bin/lua5.4 -e 'assert(21 * 2 == 42); print("H8_ALPINE_LUA54_NVME_PASS")'
/usr/bin/jq -nr '"H8_ALPINE_JQ_NVME_PASS"'
/usr/bin/sqlite3 :memory: "SELECT 'H8_ALPINE_SQLITE_NVME_PASS';"

# The physical namespace and the filesystem are intentionally read-only.
if /bin/busybox mkdir /root/h8-unexpected-write; then
    /bin/busybox echo H8_ALPINE_READONLY_VIOLATION
    exit 1
fi
test ! -e /root/h8-unexpected-write
/bin/busybox echo H8_ALPINE_READONLY_NVME_PASS
/bin/busybox echo H8_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS
