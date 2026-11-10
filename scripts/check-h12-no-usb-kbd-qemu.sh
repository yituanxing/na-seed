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
expected='[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable'
qemu_pid=''
observer_pid=''
trap 'for child in "$observer_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
rc=0
NAS_QEMU_H12_NO_USB_KBD=1 \
    timeout --signal=TERM --kill-after=3s 75s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
# The negative is decided by the actual guest refusal, never by a host
# timeout. Terminating GNU timeout forwards SIGTERM to its QEMU child;
# --kill-after bounds any resistant subprocess while wait reaps the parent.
(
    while :; do
        if grep -Fq "$expected" "$serial_log"; then
            kill -TERM "$qemu_pid" 2>/dev/null || :
            exit 0
        fi
        if grep -Eq '^\[(FAIL|FAULT|EXEC REJECT|ELF REJECT)\]' "$serial_log"; then
            kill -TERM "$qemu_pid" 2>/dev/null || :
            exit 1
        fi
        sleep 0.2
    done
) &
observer_pid=$!
wait "$qemu_pid" || rc=$?
kill -TERM "$observer_pid" 2>/dev/null || :
wait "$observer_pid" 2>/dev/null || :
qemu_pid=''
observer_pid=''
tr -d '\r' < "$serial_log" > "$clean"
# The observer may stop an already-refusing guest cleanly (rc=0); the
# required real rejection marker and all no-userspace/no-fallback checks
# below remain mandatory regardless of this host-side process status.
if [ "$rc" -ne 0 ] && [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ] && [ "$rc" -ne 143 ]; then
    echo "[H12 NO-USB-KBD FAIL] QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
grep -Fq "$expected" "$clean" || {
    echo '[H12 NO-USB-KBD FAIL] missing explicit xHCI no-keyboard rejection' >&2
    tail -150 "$clean" >&2
    exit 1
}
grep -Fq '[H0 PCI] 00:04.0 vendor=1b36 device=000d class=0c0330' "$clean" &&
grep -Fq '[H12 HID BAR QUALIFIED]' "$clean" || {
    echo '[H12 NO-USB-KBD FAIL] actual QEMU xHCI controller/bar not present' >&2
    tail -150 "$clean" >&2
    exit 1
}
if grep -Eq 'H12 HID INPUT ATTACHED|H12_HID_READY_FOR_QMP|H12_HID_A_(DOWN|UP)_WITH_SYN|H12_ORIGINAL_ALPINE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0' "$clean"; then
    echo '[H12 NO-USB-KBD FAIL] original evtest saw input without real USB keyboard' >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[H12 NO-USB-KBD FAIL-CLOSED PASS] genuine xHCI controller without keyboard cannot produce evdev event0/original evtest USB input; NVMe root alone insufficient'
