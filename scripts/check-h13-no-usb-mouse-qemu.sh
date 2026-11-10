#!/bin/sh
# Genuine QEMU xHCI exists but NO usb-mouse device at all. The signed Alpine
# NVMe root is still present, so an evtest PASS would reveal a non-USB input
# fallback (VirtIO, PS/2, synthetic serial, or fabricated evdev records).
set -eu

out=build/h13-qemu-only
image="$out/naseed.iso"
serial_log="$out/h13-no-usb-mouse-uefi-serial.log"
clean="$out/h13-no-usb-mouse-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ "${NAS_QEMU_H13_USB_MOUSE_USERLAND:-0}" = 0 ] &&
[ "${NAS_QEMU_H13_XHCI_MISSING:-0}" = 0 ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] || {
    echo '[H13 NO-USB-MOUSE FAIL] ambient input or block device supplied' >&2
    exit 2
}
expected='[H13 MOUSE FAIL] required QEMU USB Boot Mouse unavailable'
qemu_pid=''
observer_pid=''
trap 'for child in "$observer_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
rc=0
NAS_QEMU_H13_USB_MOUSE_MISSING=1 \
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
    echo "[H13 NO-USB-MOUSE FAIL] QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
grep -Fq "$expected" "$clean" || {
    echo '[H13 NO-USB-MOUSE FAIL] missing explicit xHCI no-mouse rejection' >&2
    tail -150 "$clean" >&2
    exit 1
}
grep -Fq '[H0 PCI] 00:04.0 vendor=1b36 device=000d class=0c0330' "$clean" &&
grep -Fq '[H13 MOUSE BAR QUALIFIED]' "$clean" || {
    echo '[H13 NO-USB-MOUSE FAIL] actual QEMU xHCI controller/bar not present' >&2
    tail -150 "$clean" >&2
    exit 1
}
if grep -Eq 'H13 MOUSE INPUT ATTACHED|H13_MOUSE_(READY_FOR_QMP|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0' "$clean"; then
    echo '[H13 NO-USB-MOUSE FAIL] original evtest saw input without real USB mouse' >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[H13 NO-USB-MOUSE FAIL-CLOSED PASS] genuine xHCI controller without USB Boot Mouse cannot produce evdev event0/original evtest USB input; NVMe root alone insufficient'
