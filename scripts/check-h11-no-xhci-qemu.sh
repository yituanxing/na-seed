#!/bin/sh
# Same H11 UEFI ISO, but remove both xHCI and USB storage entirely.
set -eu

out=build/h11-qemu-only
image="$out/naseed.iso"
serial_log="$out/h11-no-xhci-uefi-serial.log"
clean="$out/h11-no-xhci-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ "${NAS_QEMU_H11_USB_POSITIVE:-0}" = 0 ] &&
[ "${NAS_QEMU_H11_NO_MEDIA:-0}" = 0 ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] || {
    echo '[H11 NO-XHCI FAIL] ambient block backend supplied' >&2
    exit 2
}
rc=0
NAS_QEMU_H11_NO_XHCI=1 timeout 60s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H11 NO-XHCI FAIL] real QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -180 "$clean" >&2
    exit 1
fi
grep -Fq '[H11 USB FAIL] required QEMU USB Mass Storage unavailable' "$clean" || {
    echo '[H11 NO-XHCI FAIL] missing explicit no-controller refusal' >&2
    tail -180 "$clean" >&2
    exit 1
}
if grep -Eq 'H11 USB ROOT MOUNTED|H11_ORIGINAL_ALPINE_DYNAMIC_ELFS_USB_PASS|H11_ALPINE_(BUSYBOX|LUA54|JQ|SQLITE)_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/' "$clean"; then
    echo '[H11 NO-XHCI FAIL] Alpine ran without a genuine USB host controller' >&2
    tail -180 "$clean" >&2
    exit 1
fi
echo '[H11 NO-XHCI FAIL-CLOSED PASS] real QEMU with no xHCI or USB media cannot mount Alpine ext4 / or execute original ELF'
