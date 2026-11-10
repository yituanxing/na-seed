#!/bin/sh
# The H9 process and every dynamic ELF below must originate from readonly
# Q35 SATA AHCI ext4 `/`; its Limine rescue CPIO contains NO executable.
set -eu

test "$(/bin/busybox cat /root/h9-disk-only.txt)" = \
    'H9_SATA_SIGNED_ALPINE_DISK_ONLY_20261110'
/bin/busybox cat /root/h9-disk-only.txt
/bin/busybox cmp -s /root/h9-disk-only.txt /root/h9-copy.txt
/bin/busybox echo H9_ALPINE_BUSYBOX_SATA_PASS

/usr/bin/lua5.4 -e 'assert(21 * 2 == 42); print("H9_ALPINE_LUA54_SATA_PASS")'
/usr/bin/jq -nr '"H9_ALPINE_JQ_SATA_PASS"'
/usr/bin/sqlite3 :memory: "SELECT 'H9_ALPINE_SQLITE_SATA_PASS';"

if /bin/busybox mkdir /root/h9-unexpected-write; then
    /bin/busybox echo H9_ALPINE_READONLY_VIOLATION
    exit 1
fi
test ! -e /root/h9-unexpected-write
/bin/busybox echo H9_ALPINE_READONLY_SATA_PASS
/bin/busybox echo H9_ORIGINAL_ALPINE_DYNAMIC_ELFS_SATA_PASS
