#!/bin/sh
# Actual unmodified, checksum-pinned Debian 13 static BusyBox against a
# genuine ext4 mount INSIDE x86-64 QEMU, not a bespoke C syscall fixture.
# Host side only prepares the official ELF, CPIO, ext4 image and UEFI ISO.
set -eu
work=build/lwext4-busybox-x86
image=build/external/lwext4/ext4.img
test -s "$image"
mkdir -p "$work"
sh scripts/fetch-busybox-fixture.sh amd64 "$work/busybox"
sh scripts/stage-busybox-rootfs.sh "$work/busybox" "$work"
make NAS_ENABLE_LWEXT4=1 kernel build/deps/limine-binary/BOOTX64.EFI
for case_name in cat ls; do
    stage="$work/$case_name/iso"
    mkdir -p "$stage/EFI/BOOT" "$stage/boot/limine"
    cp build/kernel.elf "$stage/boot/kernel"
    cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
    cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
    cp "$work/rootfs.cpio" "$stage/boot/rootfs.cpio"
    cp "$image" "$stage/boot/ext4.img"
    case "$case_name" in
        cat) args='cat /mnt/sandbox/hardlink'
             expected='NaSeed VFS and BlockDevice real lwext4 data' ;;
        ls) args='ls -l /mnt/sandbox'
            expected='hardlink' ;;
    esac
    cat >"$stage/boot/limine/limine.conf" <<CFG
timeout: 0
/NaSeed unmodified pinned Debian BusyBox on real ext4
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/busybox $args
    module_path: boot():/boot/rootfs.cpio
    module_path: boot():/boot/ext4.img
CFG
    iso="$work/$case_name/test.iso"
    log="$work/$case_name/serial.log"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout 45s sh scripts/run.sh "$iso" >"$log" 2>&1 < /dev/null || rc=$?
    tr -d '\r' < "$log" > "$work/$case_name/clean.log"
    clean="$work/$case_name/clean.log"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[LWEXT4 GUEST] actual Limine ext4 image mounted at /mnt' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       ! grep -Fq "$expected" "$clean" ||
       grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$clean"; then
        echo "[LWEXT4 REAL BUSYBOX FAIL] case=$case_name rc=$rc" >&2
        tail -130 "$clean" >&2
        exit 1
    fi
    echo "[LWEXT4 REAL BUSYBOX PASS] verified Debian BusyBox $args"
done
