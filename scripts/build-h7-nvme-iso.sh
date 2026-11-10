#!/bin/sh
# Build the isolated H7 QEMU UEFI image.  The ext4 disk stays OUTSIDE Limine:
# its original Debian binaries can only be read through real NVMe LBA I/O.
set -eu

out=build/h7-qemu-only
stage="$out/uefi-stage"
kernel="$out/kernel.elf"
image="$out/naseed.iso"
rootfs="$out/fixture/rescue/rootfs.cpio"
efi="$out/deps/limine-binary/BOOTX64.EFI"
cdrom="$out/deps/limine-binary/limine-uefi-cd.bin"
test -s "$kernel" && test -s "$rootfs" && test -s "$efi" &&
test -s "$cdrom" && test -s "$out/h7-ext4.img" || {
    echo '[H7 ISO FAIL] required isolated H7 kernel/UEFI/CPIO/media missing' >&2
    exit 1
}
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$kernel" "$stage/boot/kernel"
cp "$rootfs" "$stage/boot/rootfs.cpio"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/limine-uefi-cd.bin"
cp limine-h7-nvme.conf "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo '[H7 ISO PASS] UEFI kernel+rescue only; no ext4 Limine module; NVMe media exposed by QEMU alone'
