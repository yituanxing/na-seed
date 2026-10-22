#!/bin/sh
# x86-64 first; no three-ISA wait to discover editor/TTY requirements.
set -eu
sh scripts/build-external-nano.sh amd64
sh scripts/stage-external-nano-rootfs.sh
python3 scripts/nano-pty-oracle.py build/external/nano/amd64/nano
make kernel build/deps/limine-binary/BOOTX64.EFI
work=build/external/nano/amd64
stage="$work/stage"
iso="$work/nano.iso"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
cp "$work/rootfs.cpio" "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'LIMINE'
timeout: 0
/NaSeed actual GNU nano editor
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/nano-init
    module_path: boot():/boot/rootfs.cpio
LIMINE
xorriso -as mkisofs -R -r -J   --efi-boot boot/limine/limine-uefi-cd.bin   --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/nano-serial-check.py "$iso"
