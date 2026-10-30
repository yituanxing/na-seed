#!/bin/sh
# Genuine NaSeed native x86 QEMU syscall, VFS, original littlefs, BlockDevice.
set -eu
root=build/littlefs-guest-x86
mkdir -p "$root"/stage/boot/limine "$root"/stage/EFI/BOOT "$root"/rootfs/bin "$root"/rootfs/flash
make -j2 NAS_ENABLE_LITTLEFS=1 OUT="$root/kernel-build" kernel
make build/deps/limine-binary/BOOTX64.EFI
cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin \
  -I kernel -I third_party/littlefs \
  scripts/make-littlefs-image.c ports/littlefs/block_adapter.c \
  kernel/block/device.c kernel/block/ramdisk.c kernel/lib/string.c \
  third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c \
  -o "$root/image-maker"
"$root/image-maker" "$root/stage/boot/naseed-littlefs.img"
if [ -n "${NAS_LITTLEFS_NATIVE_PROBE:-}" ]; then
    test -s "$NAS_LITTLEFS_NATIVE_PROBE"
    cp "$NAS_LITTLEFS_NATIVE_PROBE" "$root/rootfs/bin/littlefs-probe"
    expected='[PASS] LITTLEFS portable native ring3 Linux ELF via VFS'
else
    clang --target=x86_64-unknown-none-elf -c user/littlefs-guest-read.S -o "$root/probe.o"
    ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld "$root/probe.o" -o "$root/rootfs/bin/littlefs-probe"
    expected='[PASS] LITTLEFS real ring3 openat/read/getdents64 via generic VFS'
fi
(cd "$root/rootfs" && find . -print | cpio -o -H newc) > "$root/stage/boot/rootfs.cpio"
cp "$root/kernel-build/kernel.elf" "$root/stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$root/stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$root/stage/boot/limine/"
cat > "$root/stage/boot/limine/limine.conf" <<'CONF'
timeout: 0
/NaSeed original littlefs native VFS and Linux ELF syscall acceptance
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/littlefs-probe
    module_path: boot():/boot/rootfs.cpio
    module_path: boot():/boot/naseed-littlefs.img
CONF
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$root/guest.iso" "$root/stage" >/dev/null 2>&1
rc=0
timeout 45s sh scripts/run.sh "$root/guest.iso" >"$root/serial.log" 2>&1 || rc=$?
tr -d '\r' < "$root/serial.log" > "$root/clean.log"
if [ "$rc" -ne 1 ] ||
   ! grep -Fq '[PASS] LITTLEFS native kernel VFS inode read from original filesystem' "$root/clean.log" ||
   ! grep -Fq "$expected" "$root/clean.log" ||
   ! grep -Fq '[LITTLEFS GUEST] clean unmount and BlockDevice release' "$root/clean.log" ||
   ! grep -Fq '[PASS] cmdline userspace exit 0' "$root/clean.log" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$root/clean.log"; then
  echo "[LITTLEFS NATIVE QEMU FAIL] rc=$rc" >&2
  tail -100 "$root/clean.log" >&2
  exit 1
fi
echo '[LITTLEFS GUEST NATIVE PASS] x86_64 ring3 Linux ELF openat/read/getdents64 -> real VFS -> unmodified LittleFS -> BlockDevice'
