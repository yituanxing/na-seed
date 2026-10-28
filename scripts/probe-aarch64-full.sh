#!/bin/sh
# The complete existing ELF/exec/Task/VM/FS/syscall implementation under
# AArch64 real QEMU EL0, not a stand-alone EL0 smoke mini-kernel.
set -eu
if { [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] ||
       [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
       [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; } &&
   [ -s build/port-aarch64/full/kernel.elf ]; then
    if [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
        [ "${NAS_ENABLE_LITTLEFS:-0}" = 1 ] ||
          { echo '[LITTLEFS ARM64] opt-in required' >&2; exit 1; }
    else
        [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
          { echo '[LWEXT4 ARM64] opt-in required' >&2; exit 1; }
    fi
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
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    test -s "$NAS_VIRTIO_BLK_IMAGE"
    test -s "$NAS_LWEXT4_RW_PROBE"
    mcopy -i "$disk@@1048576" "$NAS_LWEXT4_RW_PROBE" ::/boot/ext4-write-probe.elf
    cat > "$dir/lwext4-guest.conf" <<'HW_CFG'
timeout: 0
/NaSeed genuine persistent VirtIO-Blk ext4 Linux ELF
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ext4-write-probe
    module_path: boot():/boot/ext4-write-probe.elf
HW_CFG
    mcopy -i "$disk@@1048576" "$dir/lwext4-guest.conf" ::/boot/limine/limine.conf
elif [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
    test -s "$NAS_LITTLEFS_TEST_IMAGE"
    test -s "$NAS_LITTLEFS_PROBE"
    mcopy -i "$disk@@1048576" "$NAS_LITTLEFS_PROBE" ::/boot/littlefs-probe.elf
    mcopy -i "$disk@@1048576" "$NAS_LITTLEFS_TEST_IMAGE" ::/boot/naseed-littlefs.img
    cat > "$dir/littlefs-guest.conf" <<'LFS_CFG'
timeout: 0
/NaSeed independent LittleFS and common native Linux ELF
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/littlefs-probe
    module_path: boot():/boot/littlefs-probe.elf
    module_path: boot():/boot/naseed-littlefs.img
LFS_CFG
    mcopy -i "$disk@@1048576" "$dir/littlefs-guest.conf" ::/boot/limine/limine.conf
elif [ -n "${NAS_LWEXT4_RW_PROBE:-}" ]; then
    test -s "$NAS_LWEXT4_TEST_IMAGE"
    test -s "$NAS_LWEXT4_RW_PROBE"
    mcopy -i "$disk@@1048576" "$NAS_LWEXT4_RW_PROBE" ::/boot/ext4-write-probe.elf
    mcopy -i "$disk@@1048576" "$NAS_LWEXT4_TEST_IMAGE" ::/boot/naseed-ram-rw.ext4
    cat > "$dir/lwext4-guest.conf" <<'RW_CFG'
timeout: 0
/NaSeed shared portable ELF via genuine native ext4 RAM write
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ext4-write-probe
    module_path: boot():/boot/ext4-write-probe.elf
    module_path: boot():/boot/naseed-ram-rw.ext4
RW_CFG
    mcopy -i "$disk@@1048576" "$dir/lwext4-guest.conf" ::/boot/limine/limine.conf
elif [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
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
# Additional raw ext4 disk is NOT a Limine module. QEMU maps a second real
# virtio-mmio block transport alongside the firmware boot ESP.
set --
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    set -- -global virtio-mmio.force-legacy=false \
           -drive "if=none,id=ext4disk,format=raw,file=$NAS_VIRTIO_BLK_IMAGE" \
           -device virtio-blk-device,drive=ext4disk
fi
set +e
timeout "${limit}s" qemu-system-aarch64 \
    -machine virt -cpu cortex-a57 -m 512M -smp 1 \
    -display none -serial stdio -monitor none -nic none \
    -semihosting-config enable=on,target=native -bios "$fw" \
    -drive if=none,id=esp,format=raw,file="$disk" \
    -device virtio-blk-device,drive=esp "$@" >"$log" 2>&1 < /dev/null &
pid=$!
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" "$limit" \
      '[VIRTIO-BLK] persistent block device write+flush enabled' \
      '[PASS] common VFS ext4 RAM RW native Linux ELF' \
      '[PASS] cmdline userspace exit 0' \
      '[ARM64] userspace debug-exit status=0'
elif [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" "${limit:-90}" \
      '[PASS] LITTLEFS native kernel VFS inode read from original filesystem' \
      '[PASS] LITTLEFS portable native ring3 Linux ELF via VFS' \
      '[LITTLEFS GUEST] clean unmount and BlockDevice release' \
      '[PASS] cmdline userspace exit 0' \
      '[ARM64] userspace debug-exit status=0'
elif [ -n "${NAS_LWEXT4_RW_PROBE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" "$limit" \
      '[INIT] creating common task/VM and architecture user entry' \
      '[PASS] common VFS ext4 RAM RW native Linux ELF' \
      '[PASS] cmdline userspace exit 0' \
      '[ARM64] userspace debug-exit status=0'
else
    sh scripts/port-boot-until.sh "$log" "$pid" "$limit" \
 '[INIT] creating common task/VM and architecture user entry' \
 '[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit' \
 '[ARM64] userspace debug-exit status=0'
fi
monitor=$?
kill "$pid" 2>/dev/null || :
wait "$pid" 2>/dev/null || :
set -e
if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$log" ||
   { [ -n "${NAS_LWEXT4_RW_PROBE:-}" ] &&
     [ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
     ! grep -Fq '[LWEXT4 GUEST] explicit ephemeral RW RamDisk only; NOT persistent storage' "$log"; } ||
   { [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] &&
     ! grep -Fq '[PASS] LWEXT4 guest VFS real ext4 inode read' "$log"; } ||
   { [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ] &&
     ! grep -Fq '[PASS] LITTLEFS portable native ring3 Linux ELF via VFS' "$log"; }; then
    echo '[ARM64 FULL FAIL] common ELF/Task/syscall usermode did not execute' >&2
    tail -100 "$log" >&2
    exit 1
fi
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    echo '[VIRTIO-BLK PHYSICAL QEMU PASS] ARM64 real disk syscall+flush'
elif [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
    echo '[LITTLEFS NATIVE VFS PASS] ARM64 actual unprivileged Linux ELF'
elif [ -n "${NAS_LWEXT4_RW_PROBE:-}" ]; then
    echo '[LWEXT4 COMMON NATIVE RW PASS] ARM64 actual unprivileged Linux ELF'
else
    echo '[PASS] AArch64 real EL0 full shared-kernel native user hello'
fi
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
    echo '[LWEXT4 REAL GUEST PASS] aarch64 Limine ext4 image -> common VFS -> upstream lwext4'
fi
