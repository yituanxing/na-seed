#!/bin/sh
# Boot the whole RV64 kernel through the SAME task/ELF/FS/syscall path as x86.
# Fail unless actual U-mode ELF executes write("hello\\n") and exit(0).
set -eu
sh scripts/build-riscv64-full.sh
dir=build/port-riscv64/full
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
      https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
efi=build/deps/limine-binary/BOOTRISCV64.EFI
test -s "$efi"
disk="$dir/esp.img"
truncate -s 128M "$disk"
sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
mformat -i "$disk@@1048576" -F ::
mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$disk@@1048576" "$efi" ::/EFI/BOOT/BOOTRISCV64.EFI
mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
mcopy -i "$disk@@1048576" "$dir/user-hello.elf" ::/boot/user-hello.elf
mcopy -i "$disk@@1048576" limine-riscv64-full.conf ::/boot/limine/limine.conf
firmware=/usr/share/qemu-efi-riscv64
test -s "$firmware/RISCV_VIRT_CODE.fd"
cp "$firmware/RISCV_VIRT_VARS.fd" "$dir/vars.fd"
log="$dir/serial.log"
set +e
timeout 90s qemu-system-riscv64 \
  -machine virt,acpi=off -m 512M -smp 1 -display none -serial stdio -monitor none \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware/RISCV_VIRT_CODE.fd" \
  -drive if=pflash,format=raw,unit=1,file="$dir/vars.fd" \
  -drive if=none,id=esp,format=raw,file="$disk" \
  -device virtio-blk-device,drive=esp >"$log" 2>&1 &
pid=$!
sh scripts/port-boot-until.sh "$log" "$pid" 90 \
 '[INIT] creating common task/VM and architecture user entry' \
 '[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit' \
 '[RV64] userspace debug-exit status=0'
monitor=$?
kill "$pid" 2>/dev/null || :
wait "$pid" 2>/dev/null || :
set -e
if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$log"; then
    echo '[RV64 FULL FAIL] full kernel userland execution failed' >&2
    tail -100 "$log" >&2
    exit 1
fi
echo '[PASS] RV64 full common ELF/task/FS/syscall kernel real QEMU user hello'
