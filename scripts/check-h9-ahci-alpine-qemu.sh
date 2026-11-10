#!/bin/sh
# Acceptance requires REAL QEMU/OVMF SATA AHCI -> readonly ext4 `/` ->
# original signed Alpine dynamic musl ELF and four actual Linux programs.
# Host-only fake AHCI tests, ISO presence and CPIO ELF are not substitutes.
set -eu

out=build/h9-qemu-only
image="$out/naseed.iso"
disk="$out/h9-alpine.ext4"
serial_log="$out/h9-qemu-uefi-serial.log"
clean="$out/h9-qemu-uefi-clean.log"
for path in "$image" "$disk" "$out/h9-media-before.sha256" \
            "$out/h9-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H9 QEMU FAIL] missing or symlinked artifact $path" >&2
        exit 2
    }
done
[ ! -L "$serial_log" ] && [ ! -L "$clean" ] || {
    echo '[H9 QEMU FAIL] symlink serial log refused' >&2
    exit 2
}
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] || {
    echo '[H9 QEMU FAIL] ambient VirtIO block backend supplied' >&2
    exit 2
}
for var in NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME \
           NAS_QEMU_H6_TWO_NVME NAS_QEMU_H7_NVME NAS_QEMU_H8_NVME; do
    eval 'value=${'"$var"'-0}'
    [ "$value" = 0 ] || {
        echo "[H9 QEMU FAIL] ambient NVMe profile $var" >&2
        exit 2
    }
done
(cd "$out" && sha256sum -c h9-media-before.sha256)
sha256sum -c "$out/h9-original-alpine-elfs.sha256"

rc=0
NAS_QEMU_H9_AHCI=1 timeout 300s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    echo "[H9 QEMU FAIL] expected guest 0 via isa-debug-exit rc=1, observed $rc" >&2
    tail -180 "$clean" >&2
    exit 1
fi
for required in \
    '[H9 AHCI BLOCK ATTACHED] name=ahci0 read-only' \
    '[H9 AHCI ROOT MOUNTED] / read-only source=ahci0' \
    '[H9 AHCI PT_INTERP] /lib/ld-musl-x86_64.so.1' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    'H9_SATA_SIGNED_ALPINE_DISK_ONLY_20261110' \
    'H9_ALPINE_BUSYBOX_SATA_PASS' \
    'H9_ALPINE_LUA54_SATA_PASS' \
    'H9_ALPINE_JQ_SATA_PASS' \
    'H9_ALPINE_SQLITE_SATA_PASS' \
    'H9_ALPINE_READONLY_SATA_PASS' \
    'H9_ORIGINAL_ALPINE_DYNAMIC_ELFS_SATA_PASS' \
    '[H9 AHCI QUIESCED RELEASED]' \
    '[H9 AHCI USERLAND SHUTDOWN]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H9 QEMU FAIL] missing real guest evidence: $required" >&2
        tail -180 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]|H9_ALPINE_READONLY_VIOLATION' "$clean" ||
   grep -Fq '[VIRTIO-BLK] persistent block device write+flush enabled' "$clean" ||
   grep -Eq '\[H7 NVME|\[H8 NVME|\[H7 NVME EXT4 MOUNTED\]' "$clean"; then
    echo '[H9 QEMU FAIL] guest fault, readonly violation or NVMe/VirtIO fallback' >&2
    tail -180 "$clean" >&2
    exit 1
fi

# The exact 128MiB SATA image must survive real ATA READ DMA EXT, dynamic
# ELF mapping and rejected mkdir byte-for-byte unchanged.
(cd "$out" && sha256sum -c h9-media-before.sha256)
sha256sum -c "$out/h9-original-alpine-elfs.sha256"
echo '[H9 REAL ALPINE AHCI PASS] QEMU ICH9 AHCI SATA READ DMA EXT -> readonly ext4 / -> original Alpine BusyBox Lua jq SQLite/musl; guest exit 0; quiesced; image SHA unchanged'
tail -150 "$clean"
