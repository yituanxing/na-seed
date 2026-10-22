#!/bin/sh
# Boot a real upstream BusyBox ash interactively using a native Linux PID1
# that owns the terminal and execs BusyBox without any special kernel hook.
set -eu
binary=build/source-busybox/amd64/m18-ash-init
rootfs=build/busybox-source-parity-x86/rootfs.cpio
test -s "$binary" && test -s "$rootfs" && test -s build/kernel.elf
stage=build/m18-native-ash-stage
iso=build/m18-native-ash.iso
efi=build/deps/limine-binary/BOOTX64.EFI
cdrom=build/deps/limine-binary/limine-uefi-cd.bin
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$stage/boot/limine/"
cp "$rootfs" "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'CONFIG'
timeout: 0
/NaSeed genuine interactive BusyBox ash
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/m18-ash-init
    module_path: boot():/boot/rootfs.cpio
CONFIG
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/m18-ash-serial.py "$iso"
