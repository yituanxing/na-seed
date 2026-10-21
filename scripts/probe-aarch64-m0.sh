#!/bin/sh
# Real QEMU AArch64 UEFI + Limine + independent kernel M0 boot.
# Only CI debug I/O uses semihosting, NOT a PL011/real machine driver.
set -eu
make ARCH=aarch64 port-kernel
# AArch64 still obeys Limine's fixed high-half ELF PHDR floor.
readelf -lW build/port-aarch64/kernel.elf
dir=build/port-aarch64
mkdir -p "$dir" build/deps
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
        https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
efi=build/deps/limine-binary/BOOTAA64.EFI
test -s "$efi"
disk="$dir/esp.img"
truncate -s 128M "$disk"
sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
mformat -i "$disk@@1048576" -F ::
mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$disk@@1048576" "$efi" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
mcopy -i "$disk@@1048576" limine-aarch64.conf ::/boot/limine/limine.conf
fw=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
test -s "$fw"
log="$dir/serial.log"
set +e
timeout 90s qemu-system-aarch64 \
    -machine virt -cpu cortex-a57 -m 512M -smp 1 \
    -display none -serial stdio -monitor none \
    -semihosting-config enable=on,target=native \
    -bios "$fw" \
    -drive if=none,id=esp,format=raw,file="$disk" \
    -device virtio-blk-device,drive=esp >"$log" 2>&1
rc=$?
set -e
if ! grep -Fq '[PASS] aarch64 M0 Limine/UEFI/semihost/frame allocator' "$log"; then
    echo "[AARCH64 BOOT FAIL] missing real kernel M0 marker (QEMU=$rc)" >&2
    tail -100 "$log" >&2
    exit 1
fi
if grep -Fq '[FAIL]' "$log"; then
    echo '[AARCH64 BOOT FAIL] kernel emitted failure marker' >&2
    tail -100 "$log" >&2
    exit 1
fi
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "[AARCH64 BOOT FAIL] unexpected QEMU exit $rc" >&2
    tail -100 "$log" >&2
    exit 1
fi
echo '[PASS] AArch64 real UEFI/Limine/frame allocator QEMU boot'
