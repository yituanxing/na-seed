#!/bin/sh
# Boot the whole RV64 kernel through the SAME task/ELF/FS/syscall path as x86.
# Fail unless actual U-mode ELF executes write("hello\\n") and exit(0).
set -eu
# A specialized gate can reuse the exact opt-in kernel ELF already built
# in the earlier CI step. Normal native CI retains its fresh-build policy.
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] && [ -s build/port-riscv64/full/kernel.elf ]; then
    [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] || { echo '[LWEXT4 RV64] opt-in required' >&2; exit 1; }
else
    sh scripts/build-riscv64-full.sh
fi
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
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
    test -s "$NAS_LWEXT4_TEST_IMAGE"
    mcopy -i "$disk@@1048576" "$NAS_LWEXT4_TEST_IMAGE" ::/boot/ext4.img
    { cat limine-riscv64-full.conf
      printf '    module_path: boot():/boot/ext4.img\n'
    } > "$dir/lwext4-guest.conf"
    mcopy -i "$disk@@1048576" "$dir/lwext4-guest.conf" ::/boot/limine/limine.conf
else
    mcopy -i "$disk@@1048576" limine-riscv64-full.conf ::/boot/limine/limine.conf
fi
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
if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$log" ||
   { [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] &&
     ! grep -Fq '[PASS] LWEXT4 guest VFS real ext4 inode read' "$log"; }; then
    echo '[RV64 FULL FAIL] full kernel userland execution failed' >&2
    tail -100 "$log" >&2
    exit 1
fi
echo '[PASS] RV64 full common ELF/task/FS/syscall kernel real QEMU user hello'
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
    echo '[LWEXT4 REAL GUEST PASS] riscv64 Limine ext4 image -> common VFS -> upstream lwext4'
fi
