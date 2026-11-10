#!/bin/sh
# Real x86-64 QEMU+OVMF H7 acceptance.  Two original Debian static ELFs must
# execute FROM the read-only NVMe ext4 VFS, not from Limine/CPIO/host Linux.
set -eu

out=build/h7-qemu-only
image="$out/naseed.iso"
serial_log="$out/h7-qemu-uefi-serial.log"
clean="$out/h7-qemu-uefi-clean.log"
test -s "$image" && test -s "$out/h7-ext4.img" &&
test -s "$out/h7-media-before.sha256" || {
    echo '[H7 QEMU FAIL] fixed H7 UEFI image or NVMe fixture missing' >&2
    exit 1
}
[ ! -L "$out/h7-ext4.img" ] && [ ! -L "$serial_log" ] || {
    echo '[H7 QEMU FAIL] symlink image/log refused' >&2
    exit 2
}
for var in NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE; do
    eval 'value=${'"$var"'-}'
    [ -z "$value" ] || {
        echo "[H7 QEMU FAIL] unexpected ambient $var" >&2
        exit 2
    }
done
for var in NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME NAS_QEMU_H6_TWO_NVME; do
    eval 'value=${'"$var"'-0}'
    [ "$value" = 0 ] || {
        echo "[H7 QEMU FAIL] unexpected ambient $var" >&2
        exit 2
    }
done
sha256sum -c "$out/h7-media-before.sha256"
sha256sum -c "$out/h7-elfs.sha256"

rc=0
NAS_QEMU_H7_NVME=1 timeout 180s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    echo "[H7 QEMU FAIL] expected real PID1 exit status 0/isa-debug-exit rc=1; got rc=$rc" >&2
    tail -160 "$clean" >&2
    exit 1
fi
for required in \
    '[H5 PCI BAR QUALIFIED] bus=0 slot=5 function=0 BAR0=' \
    '[H7 NVME BOOT CONTINUE]' \
    '[H7 NVME EXT4 MOUNTED] /mnt read-only source=nvme0' \
    '[EXEC] replaced process with lwext4 ELF: /mnt/bin/busybox' \
    '[EXEC] replaced process with lwext4 ELF: /mnt/bin/bash' \
    'H7_NVME_EXT4_DEBIAN_BUSYBOX_DISK_ONLY_20261110' \
    'H7_DEBIAN_BASH_STATIC_NVME_PASS' \
    'H7_NVME_RO_GUEST_MKDIR_REFUSED' \
    'H7_ORIGINAL_TWO_ELFS_EXECUTED_FROM_NVME' \
    '[H7 NVME USERLAND SHUTDOWN] ext4 unmounted; controller quiesced' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H7 QEMU FAIL] missing actual guest evidence: $required" >&2
        tail -180 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]|H7_NVME_READONLY_VIOLATION' "$clean" ||
   grep -Fq '[VIRTIO-BLK] persistent block device write+flush enabled' "$clean"; then
    echo '[H7 QEMU FAIL] guest fault or unexpected writable/virtio backend' >&2
    tail -180 "$clean" >&2
    exit 1
fi
# Kernel source/exec trace plus unique root-only fixture prevents passing with
# the rescue initramfs or a spoofed serial string. QEMU disk bytes MUST remain
# unchanged despite real ext4 reads, executable pages and rejected mkdir.
sha256sum -c "$out/h7-media-before.sha256"
echo '[H7 REAL NVME USERLAND PASS] QEMU UEFI relocated NVMe -> real readonly ext4 -> original Debian BusyBox + Bash ELF -> native Linux syscalls; disk SHA unchanged'
tail -130 "$clean"
