#!/bin/sh
# Entire NaSeed x86 kernel -> ring3 Linux ELF -> VFS -> unmodified RamFS.
# A CPIO root supplies only the executable and covered mountpoint.
set -eu
root=build/ramfs-native-x86
mkdir -p "$root/stage/boot/limine" "$root/stage/EFI/BOOT" \
         "$root/rootfs/bin" "$root/rootfs/ramfs"
test -s build/ramfs-native/x86_64/probe.elf
make -j2 NAS_ENABLE_RAMFS=1 OUT="$root/kernel-build" kernel
make build/deps/limine-binary/BOOTX64.EFI
cp build/ramfs-native/x86_64/probe.elf "$root/rootfs/bin/ramfs-probe"
(cd "$root/rootfs" && find . -print | cpio -o -H newc) \
  > "$root/stage/boot/rootfs.cpio"
cp "$root/kernel-build/kernel.elf" "$root/stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI \
  "$root/stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin \
  "$root/stage/boot/limine/"
cat > "$root/stage/boot/limine/limine.conf" <<'CFG'
timeout: 0
/NaSeed original embedded RamFS through native POSIX Linux syscalls
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ramfs-probe
    module_path: boot():/boot/rootfs.cpio
CFG
xorriso -as mkisofs -R -r -J \
  --efi-boot boot/limine/limine-uefi-cd.bin \
  --efi-boot-part --efi-boot-image -o "$root/guest.iso" "$root/stage" >/dev/null 2>&1
rc=0
timeout 45s sh scripts/run.sh "$root/guest.iso" \
  > "$root/serial.log" 2>&1 || rc=$?
tr -d '\r' < "$root/serial.log" > "$root/clean.log"
if [ "$rc" -ne 1 ] ||
   ! grep -Fq '[PASS] real upstream RamFS: VFS create/IO/rename/open-pin/unlink/rmdir' "$root/clean.log" ||
   ! grep -Fq '[PASS] RAMFS portable native Linux syscalls: open-unlink/orphan/rename/dirfd/getdents' "$root/clean.log" ||
   ! grep -Fq '[PASS] cmdline userspace exit 0' "$root/clean.log" ||
   ! grep -Fq '[RAMFS GUEST] original standalone RamFS clean VFS unmount' "$root/clean.log" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]|\[RAMFS NATIVE DIAG\]' "$root/clean.log"; then
  echo "[RAMFS NATIVE SYSCALL QEMU FAIL] x86 kernel and original RamFS rc=$rc" >&2
  tail -120 "$root/clean.log" >&2
  exit 1
fi
echo '[RAMFS REAL RING3 PASS] x86-64 original RAMFS and real Linux syscalls'
