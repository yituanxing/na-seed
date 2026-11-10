#!/bin/sh
# Genuine QEMU H12 NVMe Alpine root remains; remove the xHCI controller and
# the USB keyboard, forbid all VirtIO or fallback input devices.
set -eu

out=build/h12-qemu-only
image="$out/naseed.iso"
serial_log="$out/h12-no-xhci-uefi-serial.log"
clean="$out/h12-no-xhci-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ "${NAS_QEMU_H12_USB_KBD_POSITIVE:-0}" = 0 ] &&
[ "${NAS_QEMU_H12_NO_USB_KBD:-0}" = 0 ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] || {
    echo '[H12 NO-XHCI FAIL] ambient input or block device supplied' >&2
    exit 2
}
rc=0
NAS_QEMU_H12_NO_XHCI=1 timeout 75s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H12 NO-XHCI FAIL] QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
grep -Fq '[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable' "$clean" || {
    echo '[H12 NO-XHCI FAIL] missing explicit absent-controller refusal' >&2
    tail -150 "$clean" >&2
    exit 1
}
if grep -Eq 'H12 HID INPUT ATTACHED|H12_HID_READY_FOR_QMP|H12_HID_A_(DOWN|UP)_WITH_SYN|H12_ORIGINAL_ALPINE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest' "$clean"; then
    echo '[H12 NO-XHCI FAIL] original evtest received events with no USB host controller' >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[H12 NO-XHCI FAIL-CLOSED PASS] genuine QEMU with no USB host controller cannot create USB event0 or execute signed evtest input workflow'
