#!/bin/sh
# One original SHA256-pinned Debian BusyBox binary, one unchanged program
# script, same REAL ext4 raw drive across two independent QEMU boots.
# Backends (U-Boot VirtIO-Blk, lwext4, VFS) are NOT replaced by mocks.
set -eu
test "$#" -eq 1 || {
 echo 'usage: sh scripts/probe-vfs-busybox-physical.sh amd64|riscv64|arm64' >&2
 exit 2
}
arch=$1
case "$arch" in
 amd64)
   harness=scripts/probe-x86-busybox-parity.sh
   kernel=build/virtio-x86/kernel.elf ;;
 riscv64)
   harness=scripts/probe-riscv64-busybox-matrix.sh
   kernel=build/port-riscv64/full/kernel.elf ;;
 arm64)
   harness=scripts/probe-aarch64-busybox-matrix.sh
   kernel=build/port-aarch64/full/kernel.elf ;;
 *) exit 2 ;;
esac
test -s "$kernel" || {
 echo "[REAL DISK APP] missing original VirtIO+lwext4 kernel $kernel" >&2
 exit 1
}
work="build/physical-busybox/$arch"
mkdir -p "$work/extra"
sh scripts/fetch-busybox-fixture.sh "$arch" "$work/busybox"
cp tests/vfs-busybox-disk-workload "$work/extra/vfs-busybox-disk-workload"
chmod 755 "$work/extra/vfs-busybox-disk-workload"
# A separate WRITABLE raw virtual disk, not an ext4 Limine module.
# The original ext4 fixture has /sandbox and the ext4 superblock at LBA2.
disk="build/external/lwext4/virtio-busybox-$arch.img"
cp build/external/lwext4/ext4.img "$disk"
test -s "$disk"
# The same original user ELF and two-stage workload are compared to the
# Linux host first on amd64. This checks the real applet script itself.
if [ "$arch" = amd64 ]; then
  mkdir -p "$work/oracle/mnt"
  NAS_APP_BUSYBOX="$(pwd)/$work/busybox" NAS_APP_FS="$(pwd)/$work/oracle/mnt" \
    sh tests/vfs-busybox-disk-workload create > "$work/oracle-create.log"
  NAS_APP_BUSYBOX="$(pwd)/$work/busybox" NAS_APP_FS="$(pwd)/$work/oracle/mnt" \
    sh tests/vfs-busybox-disk-workload replay > "$work/oracle-replay.log"
  grep -Fxq '[PASS] REAL VIRTIO EXT4 BUSYBOX REBOOT: persisted bytes, tar, rm, rmdir' "$work/oracle-replay.log"
fi
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ENABLE_LWEXT4=1
export NAS_ENABLE_VIRTIO_BLK=1
export BUSYBOX_PARITY_BINARY="$work/busybox"
export BUSYBOX_EXTRA_DIR="$work/extra"
export BUSYBOX_PARITY_MANIFEST=scripts/vfs-busybox-disk-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=2
# The actual pre-validated VirtIO-enabled x86 ELF is NOT the default
# shared build/kernel.elf; avoid silently testing a RAM-only kernel.
[ "$arch" != amd64 ] || export NAS_ACCEPTANCE_KERNEL="$kernel"
sh "$harness"
sh scripts/check-ext4-fsck-clean.sh "$disk"
debugfs -R 'cat /real-app-disk/phase' "$disk" > "$work/persisted-phase.txt"
grep -Fxq verified-after-reboot "$work/persisted-phase.txt"
debugfs -R 'cat /real-app-disk/src/alpha' "$disk" > "$work/persisted-content.txt"
grep -Fxq persistent-hello "$work/persisted-content.txt"
echo "[PASS] REAL PHYSICAL VFS BUSYBOX $arch: original applets, 2 boots, durable ext4, e2fsck"
