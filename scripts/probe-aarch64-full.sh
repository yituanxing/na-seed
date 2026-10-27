#!/bin/sh
# The complete existing ELF/exec/Task/VM/FS/syscall implementation under
# AArch64 real QEMU EL0, not a stand-alone EL0 smoke mini-kernel.
set -eu
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] && [ -s build/port-aarch64/full/kernel.elf ]; then
    [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] || { echo '[LWEXT4 ARM64] opt-in required' >&2; exit 1; }
else
    sh scripts/build-aarch64-full.sh
fi
dir=build/port-aarch64/full
mkdir -p "$dir"
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
mcopy -i "$disk@@1048576" "$dir/user-hello.elf" ::/boot/user-hello.elf
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
    test -s "$NAS_LWEXT4_TEST_IMAGE"
    mcopy -i "$disk@@1048576" "$NAS_LWEXT4_TEST_IMAGE" ::/boot/ext4.img
    { cat limine-aarch64-full.conf
      printf '    module_path: boot():/boot/ext4.img\n'
    } > "$dir/lwext4-guest.conf"
    mcopy -i "$disk@@1048576" "$dir/lwext4-guest.conf" ::/boot/limine/limine.conf
else
    mcopy -i "$disk@@1048576" limine-aarch64-full.conf ::/boot/limine/limine.conf
fi
fw=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
test -s "$fw"
log="$dir/serial.log"
limit=${NASEED_ARM_FULL_TIMEOUT:-45}
set +e
timeout "${limit}s" qemu-system-aarch64 \
    -machine virt -cpu cortex-a57 -m 512M -smp 1 \
    -display none -serial stdio -monitor none -nic none \
    -semihosting-config enable=on,target=native -bios "$fw" \
    -drive if=none,id=esp,format=raw,file="$disk" \
    -device virtio-blk-device,drive=esp >"$log" 2>&1 < /dev/null &
pid=$!
sh scripts/port-boot-until.sh "$log" "$pid" "$limit" \
 '[INIT] creating common task/VM and architecture user entry' \
 '[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit' \
 '[ARM64] userspace debug-exit status=0'
monitor=$?
kill "$pid" 2>/dev/null || :
wait "$pid" 2>/dev/null || :
set -e
if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$log" ||
   { [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] &&
     ! grep -Fq '[PASS] LWEXT4 guest VFS real ext4 inode read' "$log"; }; then
    echo '[ARM64 FULL FAIL] common ELF/Task/syscall usermode did not execute' >&2
    tail -100 "$log" >&2
    exit 1
fi
echo '[PASS] AArch64 real EL0 full shared-kernel native user hello'
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
    echo '[LWEXT4 REAL GUEST PASS] aarch64 Limine ext4 image -> common VFS -> upstream lwext4'
fi
