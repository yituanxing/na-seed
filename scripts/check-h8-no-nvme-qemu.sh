#!/bin/sh
# Negative H8: same ISO, no NVMe controller and no alternate virtio-blk disk.
# Rescue CPIO has no ELF interpreter/application and must not satisfy H8.
set -eu

out=build/h8-qemu-only
image="$out/naseed.iso"
serial_log="$out/h8-no-nvme-uefi-serial.log"
clean="$out/h8-no-nvme-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2

[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_H8_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H7_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H2_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H4_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H5_PCI_NVME:-0}" = 0 ] &&
[ "${NAS_QEMU_H6_TWO_NVME:-0}" = 0 ] || {
    echo '[H8 NEGATIVE FAIL] ambient block controller/profile supplied' >&2
    exit 2
}

rc=0
timeout 45s sh scripts/run.sh "$image" > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H8 NEGATIVE FAIL] QEMU did not fail/halt within bound rc=$rc" >&2
    tail -120 "$clean" >&2
    exit 1
fi
if ! grep -Fq '[H3 NVME FAIL] required QEMU admin test failed' "$clean"; then
    echo '[H8 NEGATIVE FAIL] missing real no-device NVMe rejection' >&2
    tail -120 "$clean" >&2
    exit 1
fi
if grep -Eq 'H8 NVME ALPINE ROOT MOUNTED|H8_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS|H8_ALPINE_(LUA54|JQ|SQLITE)_NVME_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/' "$clean"; then
    echo '[H8 NEGATIVE FAIL] userland executed without genuine NVMe' >&2
    tail -120 "$clean" >&2
    exit 1
fi
echo '[H8 NO-NVME FAIL-CLOSED PASS] no NVMe -> no Alpine ELF, no ext4 / mount, no CPIO substitution'
