#!/bin/sh
# H11 UEFI ISO contains only the kernel and a metadata-only rescue CPIO.
# No Alpine original executable exists here or in the rescue rootfs.
set -eu

out=build/h11-qemu-only
stage="$out/uefi-stage"
kernel="$out/kernel.elf"
image="$out/naseed.iso"
rescue="$out/rescue.cpio"
efi="$out/deps/limine-binary/BOOTX64.EFI"
cdrom="$out/deps/limine-binary/limine-uefi-cd.bin"

for path in "$kernel" "$rescue" "$efi" "$cdrom" \
            "$out/h11-alpine.ext4" "$out/h11-media-before.sha256" \
            "$out/h11-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H11 ISO FAIL] missing or symlinked $path" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c h11-media-before.sha256)
sha256sum -c "$out/h11-original-alpine-elfs.sha256"

mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$kernel" "$stage/boot/kernel"
cp "$rescue" "$stage/boot/rootfs.cpio"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/limine-uefi-cd.bin"
cp limine-h11-usb.conf "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo '[H11 ISO PASS] UEFI kernel+metadata-only rescue; all signed original Alpine dynamic ELFs on external USB Mass Storage ext4'
