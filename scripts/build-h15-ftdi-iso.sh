#!/bin/sh
# H15 UEFI ISO only holds kernel, bootloader and a metadata-only rescue.
# Alpine BusyBox, its musl interpreter and signed original Alpine programs are external NVMe.
set -eu

out=build/h15-qemu-only
stage="$out/uefi-stage"
kernel="$out/kernel.elf"
image="$out/naseed.iso"
rescue="$out/rescue.cpio"
efi="$out/deps/limine-binary/BOOTX64.EFI"
cdrom="$out/deps/limine-binary/limine-uefi-cd.bin"

for path in "$kernel" "$rescue" "$efi" "$cdrom" \
            "$out/h15-alpine.ext4" "$out/h15-media-before.sha256" \
            "$out/h15-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H15 ISO FAIL] missing/symlinked input $path" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c h15-media-before.sha256)
sha256sum -c "$out/h15-original-alpine-elfs.sha256"

mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$kernel" "$stage/boot/kernel"
cp "$rescue" "$stage/boot/rootfs.cpio"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/limine-uefi-cd.bin"
cp limine-h15-ftdi.conf "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo '[H15 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine BusyBox/Lua/jq/SQLite and musl only on readonly NVMe ext4 root'
