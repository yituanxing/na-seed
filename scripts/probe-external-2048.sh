#!/bin/sh
# One real Linux PTY oracle followed by original game on true NaSeed ring3.
set -eu
sh scripts/build-external-2048.sh amd64
sh scripts/stage-external-2048-rootfs.sh
python3 scripts/2048-pty-oracle.py build/external/2048/amd64/2048
make kernel build/deps/limine-binary/BOOTX64.EFI
work=build/external/2048/amd64
stage="$work/stage"
iso="$work/game.iso"
mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
cp build/kernel.elf "$stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
cp "$work/rootfs.cpio" "$stage/boot/rootfs.cpio"
cat > "$stage/boot/limine/limine.conf" <<'LIMINE'
timeout: 0
/NaSeed native terminal game 2048
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/game-init
    module_path: boot():/boot/rootfs.cpio
LIMINE
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
python3 scripts/2048-serial-check.py "$iso"
