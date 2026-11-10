#!/bin/sh
# Real QEMU/OVMF guest acceptance: the signed original Alpine musl binaries,
# dynamically linked musl interpreter, workload data and script come from
# the discovered NVMe ext4 root, never from the Limine rescue CPIO.
set -eu

out=build/h8-qemu-only
image="$out/naseed.iso"
disk="$out/h8-alpine.ext4"
serial_log="$out/h8-qemu-uefi-serial.log"
clean="$out/h8-qemu-uefi-clean.log"

for file in "$image" "$disk" "$out/h8-media-before.sha256" \
            "$out/h8-original-alpine-elfs.sha256"; do
    test -s "$file" && [ ! -L "$file" ] || {
        echo "[H8 QEMU FAIL] absent or symlinked H8 artifact $file" >&2
        exit 2
    }
done
[ ! -L "$serial_log" ] && [ ! -L "$clean" ] || {
    echo '[H8 QEMU FAIL] symlink log refused' >&2
    exit 2
}
for var in NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE; do
    eval 'value=${'"$var"'-}'
    [ -z "$value" ] || {
        echo "[H8 QEMU FAIL] unexpected ambient $var" >&2
        exit 2
    }
done
for var in NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME \
           NAS_QEMU_H6_TWO_NVME NAS_QEMU_H7_NVME; do
    eval 'value=${'"$var"'-0}'
    [ "$value" = 0 ] || {
        echo "[H8 QEMU FAIL] unexpected ambient $var" >&2
        exit 2
    }
done
(cd "$out" && sha256sum -c h8-media-before.sha256)
sha256sum -c "$out/h8-original-alpine-elfs.sha256"

rc=0
NAS_QEMU_H8_NVME=1 timeout 300s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    echo "[H8 QEMU FAIL] genuine guest exit status=0 requires isa-debug-exit rc=1, observed $rc" >&2
    tail -180 "$clean" >&2
    exit 1
fi
for required in \
    '[H5 PCI BAR QUALIFIED] bus=0 slot=5 function=0 BAR0=' \
    '[H7 NVME BOOT CONTINUE]' \
    '[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0' \
    '[H8 NVME PT_INTERP] real NVMe ext4 interpreter:' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    'H8_ALPINE_BUSYBOX_NVME_PASS' \
    'H8_ALPINE_LUA54_NVME_PASS' \
    'H8_ALPINE_JQ_NVME_PASS' \
    'H8_ALPINE_SQLITE_NVME_PASS' \
    'H8_ALPINE_READONLY_NVME_PASS' \
    'H8_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS' \
    '[H8 NVME ALPINE ROOT SHUTDOWN] ext4 unmounted; controller quiesced' \
    '[H7 NVME QUIESCED RELEASED]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H8 QEMU FAIL] missing real guest evidence: $required" >&2
        tail -180 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]|H8_NVME_READONLY_VIOLATION|H8_ALPINE_READONLY_VIOLATION' "$clean" ||
   grep -Fq '[VIRTIO-BLK] persistent block device write+flush enabled' "$clean"; then
    echo '[H8 QEMU FAIL] guest fault, read-only violation, or unexpected virtio-blk fallback' >&2
    tail -180 "$clean" >&2
    exit 1
fi

# Disk bytes must survive real MMIO NVMe reads, ELF PT_INTERP loads, fork/exec,
# ext4 read-only path lookups and quiesce completely unchanged.
(cd "$out" && sha256sum -c h8-media-before.sha256)
sha256sum -c "$out/h8-original-alpine-elfs.sha256"
echo '[H8 REAL ALPINE NVME PASS] OVMF QEMU NVMe ext4 / root -> signed original Alpine musl BusyBox Lua jq SQLite ELF -> native Linux syscalls; guest exit 0; quiesced; media SHA unchanged'
tail -150 "$clean"
