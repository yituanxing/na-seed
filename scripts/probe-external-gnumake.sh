#!/bin/sh
# First a genuinely working Linux Make, then *the same* ELF under native QEMU.
set -eu
sh scripts/build-external-gnumake.sh amd64
sh scripts/stage-external-gnumake-rootfs.sh
make kernel build/deps/limine-binary/BOOTX64.EFI
work=build/external/gnumake/amd64
stage="$work/stage"
iso="$work/gnumake.iso"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
cp "$work/rootfs.cpio" "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'LIMINE'
timeout: 0
/NaSeed GNU Make native build tool
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/make-init
    module_path: boot():/boot/rootfs.cpio
LIMINE
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/gnumake-serial-check.py "$iso"
