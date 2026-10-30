#!/bin/sh
# Run unmodified official Debian RV64 BusyBox via the same complete Linux
# task/ELF/FS/syscall core as the original x86 BusyBox regression.
set -eu
test -s build/port-riscv64/full/kernel.elf || sh scripts/build-riscv64-full.sh
sh scripts/fetch-debian-riscv64.sh
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
mcopy -i "$disk@@1048576" "$dir/busybox" ::/boot/busybox
mcopy -i "$disk@@1048576" limine-riscv64-busybox.conf ::/boot/limine/limine.conf
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
 '[EXIT] user status=0' \
 '[RV64] userspace debug-exit status=0'
monitor=$?
kill "$pid" 2>/dev/null || :
wait "$pid" 2>/dev/null || :
set -e
if [ "$monitor" -ne 0 ] || ! tr -d '\r' < "$log" | grep -Fxq 'hello' ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$log"; then
    echo '[RV64 FULL FAIL] native Debian BusyBox exited incorrectly or did not print hello' >&2
    tail -100 "$log" >&2
    exit 1
fi
echo '[PASS] RV64 original Debian BusyBox through the same full common ELF/task/FS/syscall kernel'
