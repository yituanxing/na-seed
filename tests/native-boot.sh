#!/bin/sh
# Cross-ISA native QEMU acceptance: real Limine UEFI, full shared kernel,
# and architecture-specific user ELF. A green "make kernel" is NOT boot PASS.
set -eu
[ "$#" -eq 1 ] || { echo "usage: sh tests/native-boot.sh <riscv64|aarch64>" >&2; exit 2; }
arch=$1
case "$arch" in riscv64|aarch64) ;; *) exit 2 ;; esac
out="build/port-$arch/full"
kernel="$out/kernel.elf"
user="$out/user-hello.elf"
test -s "$kernel" && test -s "$user" || {
    echo "FAIL: build the actual native kernel and user ELF first" >&2
    exit 1
}
# The repository's existing pinned Limine-protocol build dependency and
# existing binary-release extraction are reused. Do not implement an
# incompatible alternate loader for each ISA.
make ARCH="$arch" build/deps/limine-binary/BOOTX64.EFI
case "$arch" in
    riscv64) efi=BOOTRISCV64.EFI ;;
    aarch64) efi=BOOTAA64.EFI ;;
esac
test -s "build/deps/limine-binary/$efi"
dir="build/native-boot/$arch"
mkdir -p "$dir"
disk="$dir/efi.img"
rm -f "$disk"
truncate -s 128M "$disk"
mkfs.vfat -F 32 -n NASEEDBOOT "$disk" >/dev/null
mmd -i "$disk" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$disk" "build/deps/limine-binary/$efi" "::/EFI/BOOT/$efi"
mcopy -i "$disk" "$kernel" "::/boot/kernel"
mcopy -i "$disk" "$user" "::/boot/user-hello"
# Same Limine module/entry contract used by x86; no guest-side fake program.
mcopy -i "$disk" limine.conf ::/boot/limine/limine.conf
python3 tests/native-boot.py "$arch" "$disk" "$dir/serial.log"
