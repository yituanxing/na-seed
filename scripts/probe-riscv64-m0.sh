#!/bin/sh
# Real RV64 supervisor-mode boot: OpenSBI + EDK2 UEFI + Limine + NaSeed.
# Success requires the kernel's own M0 marker after frame allocation,
# NOT just the firmware booting or a cross-compiler producing an ELF.
set -eu
make ARCH=riscv64 port-kernel
dir=build/port-riscv64
mkdir -p build/deps "$dir"
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
        https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
efi=build/deps/limine-binary/BOOTRISCV64.EFI
test -s "$efi"

# GPT with a FAT32 EFI System Partition starting at LBA 2048.
disk="$dir/esp.img"
truncate -s 128M "$disk"
sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
mformat -i "$disk@@1048576" -F ::
mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$disk@@1048576" "$efi" ::/EFI/BOOT/BOOTRISCV64.EFI
mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
mcopy -i "$disk@@1048576" limine-riscv64.conf ::/boot/limine/limine.conf

firmware=/usr/share/qemu-efi-riscv64
test -s "$firmware/RISCV_VIRT_CODE.fd"
cp "$firmware/RISCV_VIRT_VARS.fd" "$dir/vars.fd"
log="$dir/serial.log"
set +e
timeout 90s qemu-system-riscv64 \
    -machine virt,acpi=off -m 512M -smp 1 \
    -display none -serial stdio -monitor none \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware/RISCV_VIRT_CODE.fd" \
    -drive if=pflash,format=raw,unit=1,file="$dir/vars.fd" \
    -drive if=none,id=esp,format=raw,file="$disk" \
    -device virtio-blk-device,drive=esp >"$log" 2>&1
rc=$?
set -e
if ! grep -Fq '[PASS] riscv64 M0 Limine/SBI/frame allocator' "$log"; then
    echo "[RV64 BOOT FAIL] missing genuine kernel M0 marker (qemu status: $rc)" >&2
    tail -100 "$log" >&2
    exit 1
fi
if grep -Fq '[FAIL]' "$log"; then
    echo '[RV64 BOOT FAIL] kernel emitted fatal diagnostic' >&2
    tail -100 "$log" >&2
    exit 1
fi
if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
    echo "[RV64 BOOT FAIL] QEMU unexpectedly exited: $rc" >&2
    tail -100 "$log" >&2
    exit 1
fi
echo '[PASS] RV64 real Limine/OpenSBI/EDK2/SBI/M0 QEMU boot'
