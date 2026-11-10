#!/bin/sh
# REAL QEMU xHCI USB Mass Storage BOT -> SCSI READ(10) -> readonly ext4 `/`
# -> original signed Alpine musl BusyBox, Lua, jq, SQLite. Host fake tests,
# SATA ISO, NVMe and rescue CPIO cannot pass this gate.
set -eu

out=build/h11-qemu-only
image="$out/naseed.iso"
disk="$out/h11-alpine.ext4"
serial_log="$out/h11-qemu-uefi-serial.log"
clean="$out/h11-qemu-uefi-clean.log"
for path in "$image" "$disk" "$out/h11-media-before.sha256" \
            "$out/h11-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H11 QEMU FAIL] missing or symlinked $path" >&2
        exit 2
    }
done
for var in NAS_VIRTIO_NET_USER NAS_VIRTIO_NET_SECOND_USER \
           NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME \
           NAS_QEMU_H6_TWO_NVME NAS_QEMU_H7_NVME NAS_QEMU_H8_NVME \
           NAS_QEMU_H9_AHCI NAS_QEMU_H10_E1000 NAS_QEMU_H10_NO_E1000; do
    eval 'value=${'"$var"'-0}'
    [ "$value" = 0 ] || {
        echo "[H11 QEMU FAIL] ambient incompatible profile $var" >&2
        exit 2
    }
done
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ ! -L "$serial_log" ] && [ ! -L "$clean" ] || {
    echo '[H11 QEMU FAIL] ambient VirtIO block or symlink serial log' >&2
    exit 2
}
(cd "$out" && sha256sum -c h11-media-before.sha256)
sha256sum -c "$out/h11-original-alpine-elfs.sha256"

rc=0
NAS_QEMU_H11_USB_POSITIVE=1 timeout 420s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    echo "[H11 QEMU FAIL] original Alpine PID1 did not exit 0 via isa-debug-exit rc=1; observed rc=$rc" >&2
    tail -220 "$clean" >&2
    exit 1
fi
for required in \
    '[H11 XHCI BAR QUALIFIED]' \
    '[H11 USB BLOCK ATTACHED] name=usb0 read-only' \
    '[H11 USB ROOT MOUNTED] / read-only source=usb0' \
    '[H11 USB PT_INTERP] /lib/ld-musl-x86_64.so.1' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    'H11_USB_SIGNED_ALPINE_DISK_ONLY_20261110' \
    'H11_ALPINE_BUSYBOX_USB_PASS' \
    'H11_ALPINE_LUA54_USB_PASS' \
    'H11_ALPINE_JQ_USB_PASS' \
    'H11_ALPINE_SQLITE_USB_PASS' \
    'H11_ALPINE_READONLY_USB_PASS' \
    'H11_ORIGINAL_ALPINE_DYNAMIC_ELFS_USB_PASS' \
    '[H11 USB QUIESCED RELEASED]' \
    '[H11 USB USERLAND SHUTDOWN]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H11 QEMU FAIL] missing actual guest evidence: $required" >&2
        tail -220 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[DEADLOCK\]|H11_ALPINE_READONLY_VIOLATION' "$clean" ||
   grep -Eq '\[H7 NVME|\[H8 NVME|\[H9 AHCI|\[VIRTIO-BLK\]' "$clean"; then
    echo '[H11 QEMU FAIL] guest fault, media write, or a non-USB device fallback' >&2
    tail -200 "$clean" >&2
    exit 1
fi
(cd "$out" && sha256sum -c h11-media-before.sha256)
sha256sum -c "$out/h11-original-alpine-elfs.sha256"
echo '[H11 REAL USB ALPINE PASS] real QEMU xHCI/USB Mass Storage BOT SCSI READ10 -> readonly ext4 / -> original signed Alpine BusyBox Lua jq SQLite; PID1=0; DMA/BAR quiesced; raw SHA unchanged'
tail -160 "$clean"
