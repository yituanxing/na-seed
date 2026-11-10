#!/bin/sh
# Run inside NaSeed after the readonly NVMe ext4 filesystem was mounted at
# /mnt.  PID1 comes from a rescue archive, but ALL tested program invocations
# come from genuine original Debian ELF files read through the NVMe BlockDevice.
set -eu

test -x /mnt/bin/busybox
test -x /mnt/bin/bash
test -s /mnt/sandbox/host-seed.txt
/mnt/bin/busybox cat /mnt/sandbox/host-seed.txt
/mnt/bin/busybox cmp -s /mnt/sandbox/host-seed.txt /mnt/sandbox/host-copy.txt
/mnt/bin/busybox grep -Fq 'H7_NVME_EXT4_DEBIAN_BUSYBOX_DISK_ONLY_20261110' \
    /mnt/sandbox/host-seed.txt
/mnt/bin/busybox ls /mnt/bin/busybox
/mnt/bin/busybox wc -c /mnt/sandbox/host-seed.txt
/mnt/bin/bash --noprofile --norc -c \
    'read -r value < /mnt/sandbox/host-seed.txt && test "$value" = H7_NVME_EXT4_DEBIAN_BUSYBOX_DISK_ONLY_20261110 && printf "H7_DEBIAN_BASH_STATIC_NVME_PASS\n"'
# The host image is QEMU readonly=on, and the actual BlockDevice is RO.
# A write-capable fallback would violate the whole H7 experiment.
if /mnt/bin/busybox mkdir /mnt/sandbox/h7-must-not-exist; then
    /bin/busybox echo H7_NVME_READONLY_VIOLATION
    exit 1
fi
test ! -e /mnt/sandbox/h7-must-not-exist
/mnt/bin/busybox echo H7_NVME_RO_GUEST_MKDIR_REFUSED
/mnt/bin/busybox echo H7_ORIGINAL_TWO_ELFS_EXECUTED_FROM_NVME
