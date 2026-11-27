#!/bin/sh
# Real writable ext4 root acceptance from original Alpine user ELF.
# The firmware FAT boot volume is VirtIO PCI and ignored by NaSeed's
# native MMIO block driver; the one native VirtIO-MMIO disk is genuine ext4.
set -eu
[ "$#" -eq 1 ] || { echo "usage: native-root.sh <riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in riscv64|aarch64) ;; *) exit 2 ;; esac
base="build/native-boot/$arch"
test -s "$base/alpine-efi.img" && test -s "$base/alpine.cpio" &&
    test -s "$base/alpine-root/bin/busybox"
rootdisk="$base/root.ext4"
truncate -s 256M "$rootdisk"
mkfs.ext4 -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
    -d "$base/alpine-root" "$rootdisk"
e2fsck -fn "$rootdisk"
cp "$base/alpine-efi.img" "$base/root-efi.img"
# The previous boot used the baseline kernel. This must boot the newly
# compiled full feature-profile ELF, not a cached copy inside the FAT ESP.
full_kernel="build/port-$arch/full/kernel.elf"
test -s "$full_kernel"
mcopy -o -i "$base/root-efi.img" "$full_kernel" "::/boot/kernel"
sha256sum "$full_kernel"
# Kernel M191 resolves the selected executable *from the mounted block
# device*. The CPIO module is an immutable rescue archive, not an alternate
# test executable or a host-supplied stub VFS.
printf 'timeout: 0\n/NaSeed\n    protocol: limine\n    path: boot():/boot/kernel\n    cmdline: naseed.argv=/bin/busybox echo NASEED_ALPINE_ROOT_%s_OK\n    module_path: boot():/boot/alpine.cpio\n    dtb_path: boot():/boot/qemu-virt.dtb\n' "$arch" > "$base/root-limine.conf"
mcopy -o -i "$base/root-efi.img" "$base/root-limine.conf" "::/boot/limine/limine.conf"
python3 tests/native-boot.py "$arch" "$base/root-efi.img" \
    "$base/root-serial.log" root "$rootdisk"
# A successful syscall isn't enough: require a clean ext4 superblock after
# the kernel's own lwext4 unmount and flush, as with original x86 acceptance.
e2fsck -fn "$rootdisk"
echo "PASS: $arch real Alpine ext4 root, native BusyBox, durable lwext4 shutdown"
