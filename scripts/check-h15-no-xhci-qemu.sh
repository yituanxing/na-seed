#!/bin/sh
# Genuine H15 negative: QEMU has no xHCI PCI controller. Original signed Alpine NVMe still exists;
# no /dev/ttyUSB0, original BusyBox serial exchange or other fallback may run.
set -eu
out=build/h15-qemu-only
image="$out/naseed.iso"
serial_log="$out/h15-no-xhci-uefi-serial.log"
clean="$out/h15-no-xhci-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
[ "${NAS_QEMU_H15_USB_SERIAL_USERLAND:-0}" = 0 ] &&
[ "${NAS_QEMU_H15_NO_USB_SERIAL:-0}" = 0 ] &&
[ "${NAS_QEMU_H15_NO_XHCI:-0}" = 0 ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_H15_FTDI_SOCKET:-}" ] || {
    echo '[H15 NO-XHCI FAIL] ambient serial or block fixture supplied' >&2
    exit 2
}
expected='[H15 FTDI FAIL] required unique QEMU xHCI absent or ambiguous'
qemu_pid=''
observer_pid=''
trap 'for child in "$observer_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
rc=0
NAS_QEMU_H15_NO_XHCI=1 \
    timeout --signal=TERM --kill-after=3s 75s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
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
if [ "$rc" -ne 0 ] && [ "$rc" -ne 1 ] &&
   [ "$rc" -ne 124 ] && [ "$rc" -ne 143 ]; then
    echo "[H15 NO-XHCI FAIL] invalid QEMU stop rc=$rc" >&2
    tail -120 "$clean" >&2
    exit 1
fi
grep -Fq "$expected" "$clean" || {
    echo '[H15 NO-XHCI FAIL] missing explicit guest USB serial hardware rejection' >&2
    tail -140 "$clean" >&2
    exit 1
}
if grep -Fq '[H0 PCI] 00:04.0 vendor=1b36 device=000d class=0c0330' "$clean" ||
   grep -Fq '[H15 FTDI BAR QUALIFIED]' "$clean"; then
    echo '[H15 NO-XHCI FAIL] unexpectedly found USB xHCI controller' >&2
    exit 1
fi
if grep -Eq 'H15_USB_SERIAL_READY_FOR_HOST|H15_FTDI_ORIGINAL_BUSYBOX_STTY_PASS|H15_ORIGINAL_ALPINE_BUSYBOX_USB_SERIAL_NONCE_RETURNED|\[H15 USB SERIAL ATTACHED\]|\[EXEC\] replaced process with lwext4 ELF: /bin/busybox|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0' "$clean"; then
    echo '[H15 NO-XHCI FAIL] original signed Alpine ran without genuine FTDI USB serial device' >&2
    tail -140 "$clean" >&2
    exit 1
fi
echo '[H15 NO-XHCI FAIL-CLOSED PASS] original signed Alpine USB tty userspace refused when required genuine FT232BM/xHCI hardware absent; no fallback'
