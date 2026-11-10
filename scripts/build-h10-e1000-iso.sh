#!/bin/sh
# H10 UEFI contains kernel and ELF-free H8 rescue only; all signed Alpine
# executables and H10 HTTP client script are on the external NVMe ext4 root.
set -eu
out=build/h10-qemu-only
stage="$out/uefi-stage"
image="$out/naseed.iso"
for needed in \
    "$out/kernel.elf" "$out/rescue.cpio" \
    "$out/deps/limine-binary/BOOTX64.EFI" \
    "$out/deps/limine-binary/limine-uefi-cd.bin" \
    "$out/h10-alpine.ext4" "$out/h10-media-before.sha256" \
    "$out/h10-original-alpine-elfs.sha256"; do
    test -s "$needed" && [ ! -L "$needed" ] || {
        echo "[H10 ISO FAIL] missing/symlinked $needed" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c h10-media-before.sha256)
sha256sum -c "$out/h10-original-alpine-elfs.sha256"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$out/kernel.elf" "$stage/boot/kernel"
cp "$out/rescue.cpio" "$stage/boot/rootfs.cpio"
cp "$out/deps/limine-binary/BOOTX64.EFI" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$out/deps/limine-binary/limine-uefi-cd.bin" "$stage/boot/limine/limine-uefi-cd.bin"
cp limine-h10-e1000.conf "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo '[H10 ISO PASS] UEFI isolated; no Alpine or HTTP fixture in rescue; signed original BusyBox/musl and H10 script on NVMe ext4'
