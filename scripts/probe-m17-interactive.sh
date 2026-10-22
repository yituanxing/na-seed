#!/bin/sh
# Real x86 COM1 serial-input acceptance using the same source-built BusyBox
# CPIO image/firmware and real guest Linux-ABI executable, not a mock tty.
set -eu
binary=build/source-busybox/amd64/m17-input
rootfs=build/busybox-source-parity-x86/rootfs.cpio
test -s "$binary" && test -s "$rootfs" && test -s build/kernel.elf
efi=build/deps/limine-binary/BOOTX64.EFI
cdrom=build/deps/limine-binary/limine-uefi-cd.bin
stage=build/m17-tty-input-stage
iso=build/m17-tty-input.iso
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/"
cp "$rootfs" "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'CONF'
timeout: 0
/NaSeed real interactive COM1 TTY
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/m17-input
    module_path: boot():/boot/rootfs.cpio
CONF
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/m17-serial-inject.py "$iso"
