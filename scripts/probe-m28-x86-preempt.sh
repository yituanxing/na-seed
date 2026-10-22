#!/bin/sh
# Real x86 QEMU PIT acceptance: original pinned BusyBox ash executing the
# SAME POSIX script as Linux oracle. The test deliberately has a spinning
# producer; QEMU is stopped only after both IRQ evidence and user progress.
set -eu
test "${1:-amd64}" = amd64
binary=build/source-busybox/amd64/busybox
test -s "$binary"
test -s "${BUSYBOX_EXTRA_DIR:-}/m28-preempt"
make kernel build/deps/limine-binary/BOOTX64.EFI
dir=build/m28-x86-preempt
mkdir -p "$dir" "$dir/stage/boot/limine" "$dir/stage/EFI/BOOT"
sh scripts/stage-busybox-rootfs.sh "$binary" "$dir"
efi=build/deps/limine-binary/BOOTX64.EFI
cdrom=build/deps/limine-binary/limine-uefi-cd.bin
test -s "$efi" && test -s "$cdrom"
cp build/kernel.elf "$dir/stage/boot/kernel"
cp "$efi" "$dir/stage/EFI/BOOT/BOOTX64.EFI"
cp "$cdrom" "$dir/stage/boot/limine/"
cp "$dir/rootfs.cpio" "$dir/stage/boot/rootfs.cpio"
{
    printf '%s\n' 'timeout: 0' '/NaSeed genuine BusyBox PIT preemption'
    printf '%s\n' '    protocol: limine' '    path: boot():/boot/kernel'
    printf '%s\n' '    cmdline: naseed.argv=/bin/busybox sh /bin/m28-preempt'
    printf '%s\n' '    module_path: boot():/boot/rootfs.cpio'
} > "$dir/stage/boot/limine/limine.conf"
iso="$dir/preempt.iso"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$iso" "$dir/stage" >/dev/null 2>&1
python3 scripts/m28-x86-serial-check.py "$iso" "$dir/preempt.serial.log"
