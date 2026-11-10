#!/bin/sh
# REAL QEMU Q35 xHCI USB Boot Mouse -> HID Interrupt IN -> NaSeed evdev
# /dev/input/event0 -> original signed Alpine evtest on immutable NVMe ext4.
# The independent QMP oracle physically injects BTN_LEFT down+up and checks
# original program EV_KEY records, distinct SYN reports and ioctl state.
set -eu

out=build/h13-qemu-only
image="$out/naseed.iso"
serial_log="$out/h13-qemu-uefi-serial.log"
clean="$out/h13-qemu-uefi-clean.log"
oracle_log="$out/h13-qmp-evtest.log"
qmp_sock="$PWD/$out/h13-qmp.sock"
for path in "$image" "$out/h13-alpine.ext4" \
            "$out/h13-media-before.sha256" \
            "$out/h13-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H13 QEMU FAIL] missing/symlinked original media/boot artifact $path" >&2
        exit 2
    }
done
[ ! -e "$qmp_sock" ] && [ ! -L "$qmp_sock" ] &&
[ ! -L "$serial_log" ] && [ ! -L "$oracle_log" ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] || {
    echo '[H13 QEMU FAIL] stale QMP socket, log symlink or ambient VirtIO input/block' >&2
    exit 2
}
(cd "$out" && sha256sum -c h13-media-before.sha256)
sha256sum -c "$out/h13-original-alpine-elfs.sha256"

qemu_pid=''
oracle_pid=''
watchdog_pid=''
trap 'for child in "$watchdog_pid" "$oracle_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
python3 scripts/h13-qmp-evtest.py --serial "$serial_log" \
    --qmp "$qmp_sock" --timeout 270 > "$oracle_log" 2>&1 &
oracle_pid=$!
rc=0
NAS_QEMU_H13_USB_MOUSE_USERLAND=1 NAS_QEMU_QMP_SOCKET="$qmp_sock" \
    timeout --signal=TERM --kill-after=3s 360s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
# Fail fast without treating a hand-written marker as positive proof. A
# failing original evtest/QMP oracle or real fatal guest serial stops this
# single QEMU process; the real PID1=0 exit and program events are still
# checked independently below. Keep wait in foreground to reap zombies.
(
    while :; do
        if grep -Eq '^\[(H13 MOUSE FAIL|H13 XHCI FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)' "$serial_log" ||
           grep -Fq '[H13 QMP REAL USB MOUSE FAIL]' "$oracle_log"; then
            kill -TERM "$qemu_pid" 2>/dev/null || :
            exit 0
        fi
        sleep 1
    done
) &
watchdog_pid=$!
wait "$qemu_pid" || rc=$?
kill -TERM "$watchdog_pid" 2>/dev/null || :
wait "$watchdog_pid" 2>/dev/null || :
watchdog_pid=''
qemu_pid=''
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    kill -TERM "$oracle_pid" 2>/dev/null || :
    wait "$oracle_pid" 2>/dev/null || :
    oracle_pid=''
    echo "[H13 QEMU FAIL] genuine original evtest PID1 did not exit 0 rc=1, observed $rc" >&2
    tail -200 "$clean" >&2
    tail -80 "$oracle_log" >&2
    exit 1
fi
oracle_rc=0
wait "$oracle_pid" || oracle_rc=$?
oracle_pid=''
[ "$oracle_rc" -eq 0 ] &&
grep -Fq '[H13 QMP REAL USB MOUSE EVTEST PASS]' "$oracle_log" || {
    echo '[H13 QEMU FAIL] independent QMP physical USB mouse and original evtest oracle rejected run' >&2
    tail -110 "$oracle_log" >&2
    tail -160 "$clean" >&2
    exit 1
}

for required in \
    '[H13 MOUSE BAR QUALIFIED]' \
    '[H13 MOUSE USB ENUMERATED]' \
    '[H13 MOUSE INPUT ATTACHED] /dev/input/event0 QEMU USB HID Boot Mouse BUS_USB' \
    '[H13 MOUSE BOOT CONTINUE]' \
    '[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    '[EXEC] replaced process with lwext4 ELF: /usr/bin/evtest' \
    'H13_MOUSE_READY_FOR_QMP' \
    'H13_MOUSE_REL_X_WITH_SYN' \
    'H13_MOUSE_REL_Y_WITH_SYN' \
    'H13_MOUSE_BTN_DOWN_WITH_SYN' \
    'H13_MOUSE_BTN_UP_WITH_SYN' \
    'H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS' \
    '[H13 MOUSE QUIESCED RELEASED]' \
    '[H13 MOUSE USERLAND SHUTDOWN]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H13 QEMU FAIL] missing real USB HID/original software evidence: $required" >&2
        tail -200 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[H13 MOUSE FAIL|\[H13 XHCI FAIL|H13_MOUSE_EVTEST_FAIL_STAGE_' "$clean" ||
   grep -Eq '\[VIRTIO-INPUT\]|\[VIRTIO-BLK\]|\[H9 AHCI|\[H11 USB BLOCK' "$clean"; then
    echo '[H13 QEMU FAIL] fatal userland/kernel or an unauthorized input/block device fallback' >&2
    tail -180 "$clean" >&2
    exit 1
fi
(cd "$out" && sha256sum -c h13-media-before.sha256)
sha256sum -c "$out/h13-original-alpine-elfs.sha256"
echo '[H13 REAL USB MOUSE ALPINE EVTEST PASS] QEMU xHCI usb-mouse physical X/Y + BTN_LEFT(272) down/up -> real EV_REL, EV_KEY and SYN via original signed Alpine BUS_USB evtest; PID1=0, DMA/BAR released, NVMe SHA unchanged'
tail -130 "$clean"
tail -30 "$oracle_log"
