#!/bin/sh
# A UEFI kernel plus deliberately non-executable rescue metadata; the signed
# Alpine programs and musl loader are present only on QEMU's real NVMe ext4.
set -eu

out=build/h8-qemu-only
stage="$out/uefi-stage"
kernel="$out/kernel.elf"
image="$out/naseed.iso"
rootfs="$out/rescue.cpio"
efi="$out/deps/limine-binary/BOOTX64.EFI"
cdrom="$out/deps/limine-binary/limine-uefi-cd.bin"

for needed in "$kernel" "$rootfs" "$efi" "$cdrom" \
              "$out/h8-alpine.ext4" "$out/h8-media-before.sha256" \
              "$out/h8-original-alpine-elfs.sha256"; do
    test -s "$needed" && [ ! -L "$needed" ] || {
        echo "[H8 ISO FAIL] missing or symlinked H8 input $needed" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c h8-media-before.sha256)
sha256sum -c "$out/h8-original-alpine-elfs.sha256"

mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$kernel" "$stage/boot/kernel"
cp "$rootfs" "$stage/boot/rootfs.cpio"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/limine-uefi-cd.bin"
cp limine-h8-nvme.conf "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo '[H8 ISO PASS] UEFI kernel+non-executable rescue; original Alpine musl ELFs reside only in external read-only NVMe ext4'
