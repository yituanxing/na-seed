#!/bin/sh
# H9 firmware image contains kernel and metadata-only rescue. Every Alpine
# executable and its musl loader live ONLY on external QEMU AHCI SATA ext4.
set -eu

out=build/h9-qemu-only
stage="$out/uefi-stage"
kernel="$out/kernel.elf"
image="$out/naseed.iso"
rescue="$out/rescue.cpio"
efi="$out/deps/limine-binary/BOOTX64.EFI"
cdrom="$out/deps/limine-binary/limine-uefi-cd.bin"

for path in "$kernel" "$rescue" "$efi" "$cdrom" \
            "$out/h9-alpine.ext4" "$out/h9-media-before.sha256" \
            "$out/h9-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H9 ISO FAIL] missing or symlinked input $path" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c h9-media-before.sha256)
sha256sum -c "$out/h9-original-alpine-elfs.sha256"

mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$kernel" "$stage/boot/kernel"
cp "$rescue" "$stage/boot/rootfs.cpio"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/limine-uefi-cd.bin"
cp limine-h9-ahci.conf "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo '[H9 ISO PASS] UEFI kernel+ELF-free rescue; original Alpine musl ELF and loader exist only on readonly external SATA ext4'
