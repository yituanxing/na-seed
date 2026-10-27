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
for case_name in cat ls mutate; do
    stage="$work/$case_name/iso"
    mkdir -p "$stage/EFI/BOOT" "$stage/boot/limine"
    cp build/kernel.elf "$stage/boot/kernel"
    cp build/deps/limine-binary/BOOTX64.EFI "$stage/EFI/BOOT/BOOTX64.EFI"
    cp build/deps/limine-binary/limine-uefi-cd.bin "$stage/boot/limine/"
    cp "$work/rootfs.cpio" "$stage/boot/rootfs.cpio"
    ext4_module=ext4.img
    if [ "$case_name" = mutate ]; then
        # Explicitly volatile writable image; default cases are still RO.
        ext4_module=naseed-ram-rw.ext4
    fi
    cp "$image" "$stage/boot/$ext4_module"
    case "$case_name" in
        cat) args='cat /mnt/sandbox/hardlink'
             expected='NaSeed VFS and BlockDevice real lwext4 data' ;;
        ls) args='ls -l /mnt/sandbox'
            expected='hardlink' ;;
        mutate)
            # Original UNMODIFIED pinned Debian BusyBox + shell, not test
            # assembly. All operations must run in ONE live kernel mount.
            # File contents are verified by genuine cmp, not log invention.
            args="sh -c 'mkdir /mnt/sandbox/bb-dir && cp /mnt/sandbox/hardlink /mnt/sandbox/bb-dir/copied && cmp /mnt/sandbox/hardlink /mnt/sandbox/bb-dir/copied && mv /mnt/sandbox/bb-dir/copied /mnt/sandbox/bb-renamed && cmp /mnt/sandbox/hardlink /mnt/sandbox/bb-renamed && ln /mnt/sandbox/bb-renamed /mnt/sandbox/bb-hard && cmp /mnt/sandbox/hardlink /mnt/sandbox/bb-hard && rm /mnt/sandbox/bb-renamed /mnt/sandbox/bb-hard && rmdir /mnt/sandbox/bb-dir && echo NASEED_BUSYBOX_EXT4_RAM_RW_PASS'"
            expected='NASEED_BUSYBOX_EXT4_RAM_RW_PASS' ;;
    esac
    cat >"$stage/boot/limine/limine.conf" <<CFG
timeout: 0
/NaSeed unmodified pinned Debian BusyBox on real ext4
    protocol: limine
    path: boot():/boot/kernel
    cmdline: naseed.argv=/bin/busybox $args
    module_path: boot():/boot/rootfs.cpio
    module_path: boot():/boot/$ext4_module
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
       { [ "$case_name" = mutate ] && ! grep -Fq '[LWEXT4 GUEST] explicit ephemeral RW RamDisk only; NOT persistent storage' "$clean"; } ||
       grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]' "$clean"; then
        echo "[LWEXT4 REAL BUSYBOX FAIL] case=$case_name rc=$rc" >&2
        tail -130 "$clean" >&2
        exit 1
    fi
    echo "[LWEXT4 REAL BUSYBOX PASS] verified Debian BusyBox $args"
done
