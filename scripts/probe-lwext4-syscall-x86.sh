#!/bin/sh
# The complete end-to-end gate: an unprivileged standalone ELF invokes
# Linux syscalls against the real ext4 mount in a UEFI/QEMU NaSeed kernel.
# Existing host tests do NOT substitute for this runtime acceptance.
set -eu
root=build/lwext4-syscall-x86
image=build/external/lwext4/ext4.img
test -s "$image"
make NAS_ENABLE_LWEXT4=1 kernel build/deps/limine-binary/BOOTX64.EFI
mkdir -p "$root/rootfs/bin" "$root/rootfs/mnt"
mkdir -p "$root/stage/boot/limine" "$root/stage/EFI/BOOT"
clang --target=x86_64-unknown-none-elf -c user/ext4-guest-read.S \
    -o "$root/probe.o"
ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld \
    "$root/probe.o" -o "$root/rootfs/bin/ext4-probe"
(cd "$root/rootfs" && find . -print | cpio -o -H newc) \
    > "$root/rootfs.cpio"
cp build/kernel.elf "$root/stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI \
    "$root/stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin \
    "$root/stage/boot/limine/"
cp "$root/rootfs.cpio" "$root/stage/boot/rootfs.cpio"
cp "$image" "$root/stage/boot/ext4.img"
cat > "$root/stage/boot/limine/limine.conf" <<'CFG'
timeout: 0
/NaSeed guest ext4 Linux syscalls
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ext4-probe
    module_path: boot():/boot/rootfs.cpio
    module_path: boot():/boot/ext4.img
CFG
iso="$root/guest.iso"
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image -o "$iso" "$root/stage" >/dev/null 2>&1
log="$root/serial.log"
rc=0
timeout 45s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
tr -d '\r' < "$log" > "$root/clean.log"
clean="$root/clean.log"
if [ "$rc" -ne 1 ] ||
   ! grep -Fq '[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt' "$clean" ||
   ! grep -Fxq 'NaSeed VFS and BlockDevice real lwext4 data' "$clean" ||
   ! grep -Fxq '[PASS] real ext4 getdents64 via VFS' "$clean" ||
   ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$clean"; then
    echo "[LWEXT4 SYSCALL GUEST FAIL] x86 QEMU exit=$rc" >&2
    tail -130 "$clean" >&2
    exit 1
fi
echo '[LWEXT4 GUEST SYSCALL PASS] x86 UEFI: ELF openat/fstatat/read/getdents64/close -> VFS -> real ext4'
