#!/bin/sh
# Verify a REAL opt-in NaSeed kernel boots QEMU and follows
# Limine ext4 image -> RamDisk -> BlockDevice -> VFS -> lwext4 read_at.
# Existing x86 M1 genuine userspace must still reach its own exit.
set -eu
root=build/lwext4-guest-boot
image=build/external/lwext4/ext4.img
test -s "$image"
make NAS_ENABLE_LWEXT4=1 kernel build/user-hello
make build/deps/limine-binary/BOOTX64.EFI
stage="$root/iso"
mkdir -p "$stage/EFI/BOOT" "$stage/boot/limine"
cp build/kernel.elf "$stage/boot/kernel"
cp build/user-hello "$stage/boot/user-hello"
cp "$image" "$stage/boot/ext4.img"
cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
cat > "$stage/boot/limine/limine.conf" <<'CONF'
timeout: 0
/NaSeed actual lwext4 guest mount
    protocol: limine
    path: boot():/boot/kernel
    module_path: boot():/boot/user-hello
    module_path: boot():/boot/ext4.img
CONF
iso="$root/guest.iso"
log="$root/serial.log"
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
exitcode=0
timeout 40s sh scripts/run.sh "$iso" >"$log" 2>&1 || exitcode=$?
tr -d '\r' <"$log" >"$root/clean.log"
if [ "$exitcode" -ne 1 ] ||
   ! grep -Fq '[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt' "$root/clean.log" ||
   ! grep -Fq '[PASS] LWEXT4 guest VFS real ext4 inode read' "$root/clean.log" ||
   ! grep -Fxq '[USER] hello from standalone ELF via syscall' "$root/clean.log" ||
   ! grep -Fq '[MISS] syscall nr=999' "$root/clean.log" ||
   ! grep -Fq '[EXIT] user status=0' "$root/clean.log" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$root/clean.log"; then
    echo "[LWEXT4 REAL GUEST FAIL] QEMU exit=$exitcode" >&2
    tail -140 "$root/clean.log" >&2
    exit 1
fi
echo '[LWEXT4 REAL GUEST PASS] x86_64 Limine image -> actual kernel VFS -> lwext4 inode and userspace'

# Also validate the Linux system-call data plane, NOT merely kernel-mode
# VFS probing. This is a genuine unprivileged ELF using openat/fstat/read/
# lseek/write/close/exit against the live ext4 image inside QEMU.
clang --target=x86_64-unknown-none-elf -g -c user/ext4-reader.S -o "$root/ext4-reader.o"
ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld \
    "$root/ext4-reader.o" -o "$stage/boot/ext4-reader"
sed 's@module_path: boot():/boot/user-hello@module_path: boot():/boot/ext4-reader@' \
    "$stage/boot/limine/limine.conf" > "$root/ext4-reader.conf"
cp "$root/ext4-reader.conf" "$stage/boot/limine/limine.conf"
iso="$root/ext4-reader.iso"
log="$root/ext4-reader-serial.log"
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
exitcode=0
timeout 40s sh scripts/run.sh "$iso" >"$log" 2>&1 || exitcode=$?
tr -d '\r' <"$log" >"$root/ext4-reader-clean.log"
if [ "$exitcode" -ne 1 ] ||
   ! grep -Fq '[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt' "$root/ext4-reader-clean.log" ||
   ! grep -Fxq 'NaSeed VFS and BlockDevice real lwext4 data' "$root/ext4-reader-clean.log" ||
   ! grep -Fq '[EXIT] user status=0' "$root/ext4-reader-clean.log" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$root/ext4-reader-clean.log"; then
    echo "[LWEXT4 SYSCALL FAIL] real guest syscall openat/fstat/read/lseek/write/close exit=$exitcode" >&2
    tail -130 "$root/ext4-reader-clean.log" >&2
    exit 1
fi
echo '[LWEXT4 SYSCALL PASS] real ring3 openat/fstat/read/lseek/write/close on ext4'
