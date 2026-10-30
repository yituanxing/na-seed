#!/bin/sh
# Boot the whole RV64 kernel through the SAME task/ELF/FS/syscall path as x86.
# Fail unless actual U-mode ELF executes write("hello\\n") and exit(0).
set -eu
# A specialized gate can reuse the exact opt-in kernel ELF already built
# in the earlier CI step. Normal native CI retains its fresh-build policy.
if { [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ] ||
       [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ] ||
       [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ] ||
       [ -n "${NAS_RAMFS_NATIVE_PROBE:-}" ]; } &&
   [ -s build/port-riscv64/full/kernel.elf ]; then
    if [ -n "${NAS_RAMFS_NATIVE_PROBE:-}" ]; then
        [ "${NAS_ENABLE_RAMFS:-0}" = 1 ] ||
          { echo '[RAMFS RV64] opt-in required' >&2; exit 1; }
    elif [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
        [ "${NAS_ENABLE_LITTLEFS:-0}" = 1 ] ||
          { echo '[LITTLEFS RV64] opt-in required' >&2; exit 1; }
    else
        [ "${NAS_ENABLE_LWEXT4:-0}" = 1 ] ||
          { echo '[LWEXT4 RV64] opt-in required' >&2; exit 1; }
    fi
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
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    test -s "$NAS_VIRTIO_BLK_IMAGE"
    test -s "$NAS_LWEXT4_RW_PROBE"
    mcopy -i "$disk@@1048576" "$NAS_LWEXT4_RW_PROBE" ::/boot/ext4-write-probe.elf
    cat > "$dir/lwext4-guest.conf" <<'HW_CFG'
timeout: 0
/NaSeed genuine RV64 persistent VirtIO-Blk ext4 Linux ELF
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ext4-write-probe
    module_path: boot():/boot/ext4-write-probe.elf
HW_CFG
    mcopy -i "$disk@@1048576" "$dir/lwext4-guest.conf" ::/boot/limine/limine.conf
elif [ -n "${NAS_RAMFS_NATIVE_PROBE:-}" ]; then
    test -s "$NAS_RAMFS_NATIVE_PROBE"
    mcopy -i "$disk@@1048576" "$NAS_RAMFS_NATIVE_PROBE" ::/boot/ramfs-probe.elf
    cat > "$dir/ramfs-guest.conf" <<'RAMFS_CFG'
timeout: 0
/NaSeed original RAMFS through one native portable Linux ELF
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ramfs-probe
    module_path: boot():/boot/ramfs-probe.elf
RAMFS_CFG
    mcopy -i "$disk@@1048576" "$dir/ramfs-guest.conf" ::/boot/limine/limine.conf
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
set --
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    set -- -global virtio-mmio.force-legacy=false \
           -drive "if=none,id=ext4disk,format=raw,file=$NAS_VIRTIO_BLK_IMAGE" \
           -device virtio-blk-device,drive=ext4disk
fi
set +e
timeout 90s qemu-system-riscv64 \
  -machine virt,acpi=off -m 512M -smp 1 -display none -serial stdio -monitor none \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$firmware/RISCV_VIRT_CODE.fd" \
  -drive if=pflash,format=raw,unit=1,file="$dir/vars.fd" \
  -drive if=none,id=esp,format=raw,file="$disk" \
  -device virtio-blk-device,drive=esp "$@" >"$log" 2>&1 &
pid=$!
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" 90 \
      '[VIRTIO-BLK] persistent block device write+flush enabled' \
      '[PASS] common VFS ext4 RAM RW native Linux ELF' \
      '[PASS] cmdline userspace exit 0' \
      '[RV64] userspace debug-exit status=0'
elif [ -n "${NAS_RAMFS_NATIVE_PROBE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" "${limit:-90}" \
      '[PASS] real upstream RamFS: VFS create/IO/rename/open-pin/unlink/rmdir' \
      '[PASS] RAMFS portable native Linux syscalls: open-unlink/orphan/rename/dirfd/getdents' \
      '[RAMFS GUEST] original standalone RamFS clean VFS unmount' \
      '[PASS] cmdline userspace exit 0' \
      '[RV64] userspace debug-exit status=0'
elif [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" "${limit:-90}" \
      '[PASS] LITTLEFS native kernel VFS inode read from original filesystem' \
      '[PASS] LITTLEFS portable native ring3 Linux ELF via VFS' \
      '[LITTLEFS GUEST] clean unmount and BlockDevice release' \
      '[PASS] cmdline userspace exit 0' \
      '[RV64] userspace debug-exit status=0'
elif [ -n "${NAS_LWEXT4_RW_PROBE:-}" ]; then
    sh scripts/port-boot-until.sh "$log" "$pid" 90 \
      '[INIT] creating common task/VM and architecture user entry' \
      '[PASS] common VFS ext4 RAM RW native Linux ELF' \
      '[PASS] cmdline userspace exit 0' \
      '[RV64] userspace debug-exit status=0'
else
    sh scripts/port-boot-until.sh "$log" "$pid" 90 \
 '[INIT] creating common task/VM and architecture user entry' \
 '[USER] hello from standalone ELF via syscall' \
 '[MISS] syscall nr=999' \
 '[EXIT] user status=0' \
 '[RV64] userspace debug-exit status=0'
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
    echo '[RV64 FULL FAIL] full kernel userland execution failed' >&2
    tail -100 "$log" >&2
    exit 1
fi
if [ -n "${NAS_VIRTIO_BLK_IMAGE:-}" ]; then
    echo '[VIRTIO-BLK PHYSICAL QEMU PASS] RV64 real disk syscall+flush'
elif [ -n "${NAS_RAMFS_NATIVE_PROBE:-}" ]; then
    echo '[RAMFS NATIVE VFS PASS] RV64 genuine Linux ELF and independent original RAMFS'
elif [ -n "${NAS_LITTLEFS_TEST_IMAGE:-}" ]; then
    echo '[LITTLEFS NATIVE VFS PASS] RV64 actual unprivileged Linux ELF'
elif [ -n "${NAS_LWEXT4_RW_PROBE:-}" ]; then
    echo '[LWEXT4 COMMON NATIVE RW PASS] RV64 actual unprivileged Linux ELF'
else
    echo '[PASS] RV64 full common ELF/task/FS/syscall kernel real QEMU user hello'
fi
if [ -n "${NAS_LWEXT4_TEST_IMAGE:-}" ]; then
    echo '[LWEXT4 REAL GUEST PASS] riscv64 Limine ext4 image -> common VFS -> upstream lwext4'
fi
