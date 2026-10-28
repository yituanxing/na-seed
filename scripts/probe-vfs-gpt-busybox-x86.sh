#!/bin/sh
# Independent real GPT partition acceptance: unmodified original Debian
# BusyBox executes against genuine ext4 filesystem bytes in a VirtIO-PCI
# partition rather than a fake offset-0 RAM disk. No vendored code edits.
set -eu
command -v sgdisk
command -v debugfs
command -v e2fsck
work=build/gpt-busybox-x86
mkdir -p "$work/extra"
test -s build/virtio-x86/kernel.elf
test -s build/external/lwext4/ext4.img
sh scripts/fetch-busybox-fixture.sh amd64 "$work/busybox"
cp tests/vfs-busybox-disk-workload "$work/extra/vfs-busybox-disk-workload"
chmod 755 "$work/extra/vfs-busybox-disk-workload"
disk="$work/virtio-gpt.img"
truncate -s 64M "$disk"
sgdisk --clear --new=1:2048:+40M --typecode=1:8300 "$disk" >"$work/gpt-create.log"
sgdisk --verify "$disk" >"$work/gpt-before.log"
# The whole original ext4 filesystem (32MiB) is placed INSIDE the genuine
# GPT partition (40MiB), never at whole-disk LBA0.
dd if=build/external/lwext4/ext4.img of="$disk" bs=512 seek=2048 conv=notrunc status=none
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ACCEPTANCE_KERNEL=build/virtio-x86/kernel.elf
export BUSYBOX_PARITY_BINARY="$work/busybox"
export BUSYBOX_EXTRA_DIR="$work/extra"
export BUSYBOX_PARITY_MANIFEST=scripts/vfs-busybox-disk-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=2
sh scripts/probe-x86-busybox-parity.sh
for run in disk-create disk-replay; do
  log="build/busybox-source-parity-x86/$run.clean.log"
  grep -Fq '[VIRTIO-BLK] real modern PCI devices=1' "$log"
  grep -Fq '[LWEXT4 GUEST] real GPT/MBR partition ext4 disk mounted at /mnt' "$log"
  grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
  grep -Fq '[PASS] cmdline userspace exit 0' "$log"
done
sgdisk --verify "$disk" >"$work/gpt-after.log"
# Validate actual persisted bytes through the partition offset, not the
# GPT block device's raw GPT header or a synthetic guest stdout marker.
dd if="$disk" of="$work/partition-after.img" bs=512 skip=2048 count=81920 status=none
sh scripts/check-ext4-fsck-clean.sh "$work/partition-after.img"
debugfs -R 'cat /real-app-disk/phase' "$work/partition-after.img" >"$work/phase"
debugfs -R 'cat /real-app-disk/src/alpha' "$work/partition-after.img" >"$work/content"
grep -Fxq verified-after-reboot "$work/phase"
grep -Fxq persistent-hello "$work/content"
echo '[PASS] REAL GPT VIRTIO EXT4: 2 boots, original BusyBox, genuine partition BlockDevice, durable e2fsck'
