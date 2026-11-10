#!/bin/sh
# Genuine QEMU xHCI exists but NO usb-kbd device at all. The signed Alpine
# NVMe root is still present, so an evtest PASS would reveal a non-USB input
# fallback (VirtIO, PS/2, synthetic serial, or fabricated evdev records).
set -eu

out=build/h12-qemu-only
image="$out/naseed.iso"
serial_log="$out/h12-no-usb-kbd-uefi-serial.log"
clean="$out/h12-no-usb-kbd-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ "${NAS_QEMU_H12_USB_KBD_POSITIVE:-0}" = 0 ] &&
[ "${NAS_QEMU_H12_NO_XHCI:-0}" = 0 ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] || {
    echo '[H12 NO-USB-KBD FAIL] ambient input or block device supplied' >&2
    exit 2
}
rc=0
NAS_QEMU_H12_NO_USB_KBD=1 timeout 75s sh scripts/run.sh "$image" \
    > "$serial_log" 2>&1 || rc=$?
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ]; then
    echo "[H12 NO-USB-KBD FAIL] QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
grep -Fq '[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable' "$clean" || {
    echo '[H12 NO-USB-KBD FAIL] missing explicit xHCI no-keyboard rejection' >&2
    tail -150 "$clean" >&2
    exit 1
}
if grep -Eq 'H12 HID INPUT ATTACHED|H12_HID_READY_FOR_QMP|H12_HID_A_(DOWN|UP)_WITH_SYN|H12_ORIGINAL_ALPINE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest' "$clean"; then
    echo '[H12 NO-USB-KBD FAIL] original evtest saw input without real USB keyboard' >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[H12 NO-USB-KBD FAIL-CLOSED PASS] genuine xHCI controller without keyboard cannot produce evdev event0/original evtest USB input; NVMe root alone insufficient'
