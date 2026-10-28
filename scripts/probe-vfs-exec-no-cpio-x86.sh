#!/bin/sh
# Genuine x86 QEMU acceptance: first user ELF is a direct Limine module.
# NO CPIO is present. An unmodified pinned Debian BusyBox then execve()s
# the identical original BusyBox bytes stored on an independent REAL
# VirtIO-PCI ext4 disk. This exercises VFS-only runtime executable source,
# source inode lease and clean unmount on the complete kernel (not a mock).
set -eu
work=build/rootless-disk-exec-x86
mkdir -p "$work"
kernel=build/virtio-x86/kernel.elf
base=build/external/lwext4/ext4.img
test -s "$kernel"
test -s "$base"
sh scripts/fetch-busybox-fixture.sh amd64 "$work/busybox"
test "$("$work/busybox" echo ROOTLESS-EXT4-EXEC-OK)" = ROOTLESS-EXT4-EXEC-OK
disk="$work/real-ext4.img"
cp "$base" "$disk"
# debugfs may return a zero code on a failed command; compare actual bytes.
debugfs -w -R "write $work/busybox /busybox" "$disk" >"$work/debugfs-write.log" 2>&1
debugfs -R "dump /busybox $work/drive-busybox" "$disk" >"$work/debugfs-dump.log" 2>&1
cmp "$work/busybox" "$work/drive-busybox"
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ACCEPTANCE_KERNEL="$kernel"
export BUSYBOX_PARITY_BINARY="$work/busybox"
export BUSYBOX_PARITY_MANIFEST=scripts/vfs-rootless-exec-cases.tsv
export BUSYBOX_PARITY_CASE=rootless-ext4-exec
sh scripts/probe-x86-busybox-parity.sh
log=build/busybox-source-parity-x86/rootless-ext4-exec.clean.log
test -s "$log"
grep -Fq '[VIRTIO-BLK] real modern PCI devices=1' "$log"
grep -Fq '[LWEXT4 GUEST] real external VirtIO-Blk ext4 disk mounted at /mnt' "$log"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /mnt/busybox' "$log"
grep -Fxq ROOTLESS-EXT4-EXEC-OK "$log"
if grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"; then
    echo '[ARCH FAIL] no-CPIO acceptance unexpectedly mounted bootfs' >&2
    exit 1
fi
sh scripts/check-ext4-fsck-clean.sh "$disk"
debugfs -R "dump /busybox $work/reboot-busybox" "$disk" >"$work/debugfs-recheck.log" 2>&1
cmp "$work/busybox" "$work/reboot-busybox"
echo '[PASS] BOOT/EXEC ARCH: real direct ELF, NO CPIO, VFS exec from physical ext4, unmodified Debian BusyBox'
