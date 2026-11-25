#!/bin/sh
# Shared exact H7–H15 isolated UEFI ISO construction.
# Profile controls paths and historic markers only; no weakened proof gates.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
case "$profile" in
  h7) prefix=H7; config='limine-h7-nvme.conf';
    pass='[H7 ISO PASS] UEFI kernel+rescue only; no ext4 Limine module; NVMe media exposed by QEMU alone' ;;
  h8) prefix=H8; config='limine-h8-nvme.conf';
    pass='[H8 ISO PASS] UEFI kernel+non-executable rescue; original Alpine musl ELFs reside only in external read-only NVMe ext4' ;;
  h9) prefix=H9; config='limine-h9-ahci.conf';
    pass='[H9 ISO PASS] UEFI kernel+ELF-free rescue; original Alpine musl ELF and loader exist only on readonly external SATA ext4' ;;
  h10) prefix=H10; config='limine-h10-e1000.conf';
    pass='[H10 ISO PASS] UEFI isolated; no Alpine or HTTP fixture in rescue; signed original BusyBox/musl and H10 script on NVMe ext4' ;;
  h11) prefix=H11; config='limine-h11-usb.conf';
    pass='[H11 ISO PASS] UEFI kernel+metadata-only rescue; all signed original Alpine dynamic ELFs on external USB Mass Storage ext4' ;;
  h12) prefix=H12;
    config='limine-h12-usb-hid.conf'
    pass='[H12 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine evtest and musl only on readonly NVMe ext4 root' ;;
  h13) prefix=H13;
    config='limine-h13-usb-mouse.conf'
    pass='[H13 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine evtest and musl only on readonly NVMe ext4 root' ;;
  h14) prefix=H14;
    config='limine-h14-dual-hid.conf'
    pass='[H14 ISO PASS] UEFI kernel/rescue metadata; signed original Alpine evtest and musl only on readonly NVMe ext4 root' ;;
  h15) prefix=H15;
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
if [ "$profile" = h7 ]; then
    rescue="$out/fixture/rescue/rootfs.cpio"
    test -s "$kernel" && test -s "$rescue" && test -s "$efi" &&
    test -s "$cdrom" && test -s "$out/h7-ext4.img" || {
        echo '[H7 ISO FAIL] required isolated H7 kernel/UEFI/CPIO/media missing' >&2
        exit 1
    }
else
for path in "$kernel" "$rescue" "$efi" "$cdrom" \
            "$out/${profile}-alpine.ext4" "$out/${profile}-media-before.sha256" \
            "$out/${profile}-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        case "$profile" in
            h8) echo "[H8 ISO FAIL] missing or symlinked H8 input $path" >&2 ;;
            h9) echo "[H9 ISO FAIL] missing or symlinked input $path" >&2 ;;
            h10) echo "[H10 ISO FAIL] missing/symlinked $path" >&2 ;;
            h11) echo "[H11 ISO FAIL] missing or symlinked $path" >&2 ;;
            *) echo "[${prefix} ISO FAIL] missing/symlinked input $path" >&2 ;;
        esac
        exit 1
    }
done
(cd "$out" && sha256sum -c ${profile}-media-before.sha256)
sha256sum -c "$out/${profile}-original-alpine-elfs.sha256"
fi
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
