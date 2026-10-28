#!/bin/sh
# x86-64 actual QEMU q35 modern PCI -> unchanged U-Boot -> ext4 persistence.
# Separate build directory retains the existing x86 acceptance artifacts.
set -eu
root=build/lwext4-pci-x86
image=${NAS_VIRTIO_BLK_IMAGE:?real disk image required}
elf=${NAS_LWEXT4_RW_PROBE:?native Linux ELF required}
test -s "$image"
test -s "$elf"
test -s build/deps/limine.h
mkdir -p "$root/build/deps" "$root/rootfs/bin" \
         "$root/stage/boot/limine" "$root/stage/EFI/BOOT" \
         build/virtio-x86/deps
cp build/deps/limine.h build/virtio-x86/deps/limine.h
make -j2 OUT=build/virtio-x86 NAS_ENABLE_LWEXT4=1 \
     NAS_ENABLE_VIRTIO_BLK=1 kernel
cp "$elf" "$root/rootfs/bin/ext4-write-probe"
(cd "$root/rootfs" && find . -print | cpio -o -H newc) > "$root/rootfs.cpio"
cp build/virtio-x86/kernel.elf "$root/stage/boot/kernel"
cp build/deps/limine-binary/BOOTX64.EFI "$root/stage/EFI/BOOT/BOOTX64.EFI"
cp build/deps/limine-binary/limine-uefi-cd.bin "$root/stage/boot/limine/"
cp "$root/rootfs.cpio" "$root/stage/boot/rootfs.cpio"
cat > "$root/stage/boot/limine/limine.conf" <<'CFG'
timeout: 0
/NaSeed x86 real modern PCI physical ext4 disk
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/ext4-write-probe
    module_path: boot():/boot/rootfs.cpio
CFG
xorriso -as mkisofs -R -r -J \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    --efi-boot-part --efi-boot-image \
    -o "$root/guest.iso" "$root/stage" >/dev/null 2>&1
rc=0
timeout 45s sh scripts/run.sh "$root/guest.iso" >"$root/serial.log" 2>&1 || rc=$?
tr -d '\r' < "$root/serial.log" > "$root/clean.log"
clean="$root/clean.log"
if [ "$rc" -ne 1 ] ||
   ! grep -Fq '[VIRTIO-BLK] real modern PCI devices=1' "$clean" ||
   ! grep -Fq '[VIRTIO-BLK] persistent block device write+flush enabled' "$clean" ||
   ! grep -Fxq '[PASS] common VFS ext4 RAM RW native Linux ELF' "$clean" ||
   ! grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$clean" ||
   ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
   grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$clean"; then
    echo "[VIRTIO-BLK PCI REAL QEMU FAIL] exit=$rc" >&2
    tail -130 "$clean" >&2
    exit 1
fi
echo '[VIRTIO-BLK PHYSICAL QEMU PASS] x86 modern PCI disk, real ELF ext4 fsync/flush/unmount'
