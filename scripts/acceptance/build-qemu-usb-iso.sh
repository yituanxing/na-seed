#!/bin/sh
# Shared exact H12–H15 UEFI ISO construction.
# Profile controls paths and historic markers only; no weakened proof gates.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
case "$profile" in
  h12) prefix=H12; number=12;
    config='limine-h12-usb-hid.conf'
    pass='[H12 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine evtest and musl only on readonly NVMe ext4 root' ;;
  h13) prefix=H13; number=13;
    config='limine-h13-usb-mouse.conf'
    pass='[H13 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine evtest and musl only on readonly NVMe ext4 root' ;;
  h14) prefix=H14; number=14;
    config='limine-h14-dual-hid.conf'
    pass='[H14 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine evtest and musl only on readonly NVMe ext4 root' ;;
  h15) prefix=H15; number=15;
    config='limine-h15-ftdi.conf'
    pass='[H15 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine BusyBox/Lua/jq/SQLite and musl only on readonly NVMe ext4 root' ;;
  *) exit 2 ;;
esac
out=build/${profile}-qemu-only
stage="$out/uefi-stage"
kernel="$out/kernel.elf"
image="$out/naseed.iso"
rescue="$out/rescue.cpio"
efi="$out/deps/limine-binary/BOOTX64.EFI"
cdrom="$out/deps/limine-binary/limine-uefi-cd.bin"
for path in "$kernel" "$rescue" "$efi" "$cdrom" \
            "$out/${profile}-alpine.ext4" "$out/${profile}-media-before.sha256" \
            "$out/${profile}-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[${prefix} ISO FAIL] missing/symlinked input $path" >&2
        exit 1
    }
done
(cd "$out" && sha256sum -c ${profile}-media-before.sha256)
sha256sum -c "$out/${profile}-original-alpine-elfs.sha256"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp "$kernel" "$stage/boot/kernel"
cp "$rescue" "$stage/boot/rootfs.cpio"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/limine-uefi-cd.bin"
cp "$config" "$stage/boot/limine/limine.conf"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$image" "$stage" >/dev/null 2>&1
test -s "$image"
echo "$pass"
