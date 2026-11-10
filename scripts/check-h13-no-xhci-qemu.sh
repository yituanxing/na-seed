#!/bin/sh
# Genuine QEMU H13 NVMe Alpine root remains; remove the xHCI controller and
# the USB mouse, forbid all VirtIO or fallback input devices.
set -eu

out=build/h13-qemu-only
image="$out/naseed.iso"
serial_log="$out/h13-no-xhci-uefi-serial.log"
clean="$out/h13-no-xhci-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ "${NAS_QEMU_H13_USB_MOUSE_USERLAND:-0}" = 0 ] &&
[ "${NAS_QEMU_H13_USB_MOUSE_MISSING:-0}" = 0 ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] || {
    echo '[H13 NO-XHCI FAIL] ambient input or block device supplied' >&2
    exit 2
}
expected='[H13 MOUSE FAIL] required unique QEMU xHCI absent or ambiguous'
qemu_pid=''
observer_pid=''
trap 'for child in "$observer_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
rc=0
NAS_QEMU_H13_XHCI_MISSING=1 \
    timeout --signal=TERM --kill-after=3s 75s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
# Fail closed as soon as the actual guest recognizes that no qualified
# qemu-xhci exists. The observer only shortens wall time: its own marker
# can never substitute for final guest-serial and absent-hardware checks.
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
# A clean rc=0 after SIGTERM is not a guest success. Only the independent
# exact refusal marker plus absent-hardware/no-PID1 evidence below may PASS.
if [ "$rc" -ne 0 ] && [ "$rc" -ne 1 ] && [ "$rc" -ne 124 ] && [ "$rc" -ne 143 ]; then
    echo "[H13 NO-XHCI FAIL] QEMU did not refuse/halt within bound rc=$rc" >&2
    tail -150 "$clean" >&2
    exit 1
fi
grep -Fq "$expected" "$clean" || {
    echo '[H13 NO-XHCI FAIL] missing explicit absent-controller refusal' >&2
    tail -150 "$clean" >&2
    exit 1
}
if grep -Eq '\[H0 PCI\] 00:04\.0 vendor=1b36 device=000d|\[H13 MOUSE BAR QUALIFIED\]|H13 MOUSE INPUT ATTACHED|H13_MOUSE_(READY_FOR_QMP|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0' "$clean"; then
    echo '[H13 NO-XHCI FAIL] original evtest received events with no USB host controller' >&2
    tail -150 "$clean" >&2
    exit 1
fi
echo '[H13 NO-XHCI FAIL-CLOSED PASS] genuine QEMU with no USB host controller cannot create USB event0 or execute signed evtest input workflow'
