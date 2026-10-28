#!/bin/sh
# One physical GPT/MBR partition BlockDevice contract, all three native
# ISAs. The original Debian BusyBox and original upstream ext4 are NOT
# patched. Two separate QEMU boots share the SAME writeable GPT raw disk.
set -eu
test "$#" -eq 1 || {
    echo 'usage: probe-vfs-gpt-busybox.sh amd64|riscv64|arm64' >&2
    exit 2
}
arch=$1
case "$arch" in
 amd64)
   harness=scripts/probe-x86-busybox-parity.sh
   kernel=build/virtio-x86/kernel.elf
   serial_dir=build/busybox-source-parity-x86 ;;
 riscv64)
   harness=scripts/probe-riscv64-busybox-matrix.sh
   kernel=build/port-riscv64/full/kernel.elf
   serial_dir=build/port-riscv64/full/matrix-source ;;
 arm64)
   harness=scripts/probe-aarch64-busybox-matrix.sh
   kernel=build/port-aarch64/full/kernel.elf
   serial_dir=build/port-aarch64/full/matrix-source ;;
 *) echo '[GPT APP FAIL] unsupported native ISA' >&2; exit 2 ;;
esac
for util in sgdisk dd debugfs e2fsck; do command -v "$util" >/dev/null; done
test -s "$kernel" || { echo "[GPT APP FAIL] missing native kernel $kernel" >&2; exit 1; }
test -s build/external/lwext4/ext4.img
work="build/gpt-busybox/$arch"
mkdir -p "$work/extra"
sh scripts/fetch-busybox-fixture.sh "$arch" "$work/busybox"
cp tests/vfs-busybox-disk-workload "$work/extra/vfs-busybox-disk-workload"
chmod 755 "$work/extra/vfs-busybox-disk-workload"
disk="$work/virtio-gpt.img"
truncate -s 64M "$disk"
sgdisk --clear --new=1:2048:+40M --typecode=1:8300 "$disk" >"$work/gpt-create.log"
sgdisk --verify "$disk" >"$work/gpt-before.log"
# The 32MiB ext4 filesystem is INSIDE a genuine 40MiB GPT partition.
# Its first superblock is at physical LBA2048+2, never at LBA2.
dd if=build/external/lwext4/ext4.img of="$disk" bs=512 seek=2048 conv=notrunc status=none
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1
export BUSYBOX_PARITY_BINARY="$work/busybox"
export BUSYBOX_EXTRA_DIR="$work/extra"
export BUSYBOX_PARITY_MANIFEST=scripts/vfs-busybox-disk-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=2
[ "$arch" != amd64 ] || export NAS_ACCEPTANCE_KERNEL="$kernel"
sh "$harness"
for run in disk-create disk-replay; do
    log="$serial_dir/$run.clean.log"
    test -s "$log"
    grep -Fq '[LWEXT4 GUEST] real GPT/MBR partition ext4 disk mounted at /mnt' "$log"
    grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
    grep -Fq '[PASS] cmdline userspace exit 0' "$log"
done
# Only the x86 PCI platform prints the PCI-specific device probe marker.
if [ "$arch" = amd64 ]; then
    grep -Fq '[VIRTIO-BLK] real modern PCI devices=1' "$serial_dir/disk-create.clean.log"
fi
sgdisk --verify "$disk" >"$work/gpt-after.log"
# Verify physical persistence OUTSIDE QEMU, across both boots, by the GPT
# partition's sector offset. The original ext4 image is not reset.
dd if="$disk" of="$work/partition-after.img" bs=512 skip=2048 count=81920 status=none
sh scripts/check-ext4-fsck-clean.sh "$work/partition-after.img"
debugfs -R 'cat /real-app-disk/phase' "$work/partition-after.img" >"$work/phase"
debugfs -R 'cat /real-app-disk/src/alpha' "$work/partition-after.img" >"$work/content"
grep -Fxq verified-after-reboot "$work/phase"
grep -Fxq persistent-hello "$work/content"
echo "[PASS] REAL GPT VIRTIO EXT4 $arch: 2 QEMU boots, native original BusyBox, partition owner, clean GPT and e2fsck"
