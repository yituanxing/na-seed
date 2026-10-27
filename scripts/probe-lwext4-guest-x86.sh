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
   ! grep -Fq '[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit' "$root/clean.log" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$root/clean.log"; then
    echo "[LWEXT4 REAL GUEST FAIL] QEMU exit=$exitcode" >&2
    tail -140 "$root/clean.log" >&2
    exit 1
fi
echo '[LWEXT4 REAL GUEST PASS] x86_64 Limine image -> actual kernel VFS -> lwext4 inode and userspace'
