#!/bin/sh
# Negative H7: identical experimental userland image with NO NVMe controller.
# The rescue BusyBox alone must never count as physical ext4 user-program PASS.
set -eu

out=build/h7-qemu-only
image="$out/naseed.iso"
serial_log="$out/h7-no-nvme-uefi-serial.log"
clean="$out/h7-no-nvme-uefi-clean.log"
test -s "$image" || exit 2
[ ! -L "$serial_log" ] || exit 2
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_H7_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H2_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H4_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H5_PCI_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H6_TWO_NVME:-0}" = 0 ] || {
    echo '[H7 NEGATIVE FAIL] unexpected ambient disk' >&2
    exit 2
}

rc=0
timeout 32s sh scripts/run.sh "$image" > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H7 NEGATIVE FAIL] QEMU did not cleanly fail/halt rc=$rc" >&2
    tail -100 "$clean" >&2
    exit 1
fi
if ! grep -Fq '[H3 NVME FAIL] required QEMU admin test failed' "$clean" ||
   grep -Eq 'H7 NVME EXT4 MOUNTED|H7_ORIGINAL_TWO_ELFS_EXECUTED_FROM_NVME|\[EXEC\] replaced process with lwext4 ELF: /mnt/bin/' "$clean"; then
    echo '[H7 NEGATIVE FAIL] missing no-device rejection or userland ran without NVMe' >&2
    tail -100 "$clean" >&2
    exit 1
fi
echo '[H7 NO-NVME FAIL-CLOSED PASS] absent NVMe refused before mounted userland; CPIO rescue is not a substitute'
