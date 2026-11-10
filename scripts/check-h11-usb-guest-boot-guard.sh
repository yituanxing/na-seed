#!/bin/sh
# Linkage witness for H11's *separately compiled* lwext4 adapter. H11's
# external physical-root mount MUST be compiled into lwext4_guest.o; the
# common kernel having NAS_ENABLE_H11 alone is not enough. This gate caught
# the real-QEMU regression where a missing outer #if silently returned true
# without mounting usb0, then PID1 failed looking up /bin/busybox in CPIO.
set -eu
object=${1:-build/h11-qemu-only/lwext4_guest.o}
test -s "$object" && [ ! -L "$object" ] || {
    echo "[H11 LWEXT4 BUILD FAIL] missing or symlinked $object" >&2
    exit 2
}
nm -u "$object" | grep -Eq '[[:space:]]U[[:space:]]+nas_partition_probe$' || {
    echo '[H11 LWEXT4 BUILD FAIL] physical USB partition scanner is not linked into guest_boot.o' >&2
    exit 1
}
nm -u "$object" | grep -Eq '[[:space:]]U[[:space:]]+nas_block_get$' || {
    echo '[H11 LWEXT4 BUILD FAIL] actual registered USB BlockDevice lookup is not linked into guest_boot.o' >&2
    exit 1
}
strings "$object" | grep -Fq '[H11 USB ROOT MOUNTED] / read-only source=usb0' || {
    echo '[H11 LWEXT4 BUILD FAIL] H11 physical read-only ext4 root branch was optimized out' >&2
    exit 1
}
echo '[H11 LWEXT4 BUILD PASS] guest_boot.o includes real usb0 BlockDevice, partition probing and USB ext4 / mount branch'
