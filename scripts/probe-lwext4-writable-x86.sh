#!/bin/sh
# Real QEMU Linux userspace writes to an *opt-in ephemeral RAM* ext4 mount.
# All normal Limine ext4 images remain RO; no physical persistence claimed.
set -eu
root=build/lwext4-write-x86
image=build/external/lwext4/ext4.img
test -s "$image"
make NAS_ENABLE_LWEXT4=1 kernel build/deps/limine-binary/BOOTX64.EFI
mkdir -p "$root/rootfs/bin" "$root/rootfs/mnt"
mkdir -p "$root/stage/boot/limine" "$root/stage/EFI/BOOT"
clang --target=x86_64-unknown-none-elf -c user/ext4-guest-write.S -o "$root/probe.o"
ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld \
    "$root/probe.o" -o "$root/rootfs/bin/ext4-write-probe"
(cd "$root/rootfs" && find . -print | cpio -o -H newc) > "$root/rootfs.cpio"
cp build/kernel.elf "$root/stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$root/stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$root/stage/boot/limine/"
cp "$root/rootfs.cpio" "$root/stage/boot/rootfs.cpio"
cp "$image" "$root/stage/boot/naseed-ram-rw.ext4"
cat > "$root/stage/boot/limine/limine.conf" <<'CFG'
timeout: 0
/NaSeed explicit ephemeral ext4 RAM write syscall acceptance
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ext4-write-probe
    module_path: boot():/boot/rootfs.cpio
    module_path: boot():/boot/naseed-ram-rw.ext4
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
   ! grep -Fq '[LWEXT4 GUEST] explicit ephemeral RW RamDisk only; NOT persistent storage' "$clean" ||
   ! grep -Fxq '[PASS] guest ext4 RAM RW openat/write/fsync/lseek/read/close/reopen' "$clean" ||
   ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$clean"; then
    echo "[LWEXT4 REAL GUEST RW FAIL] x86 QEMU exit=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[LWEXT4 REAL GUEST RAM-RW PASS] native unprivileged Linux ELF verified filesystem mutation'
