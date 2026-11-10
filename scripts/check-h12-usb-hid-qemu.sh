#!/bin/sh
# REAL QEMU Q35 xHCI USB Boot Keyboard -> HID Interrupt IN -> NaSeed evdev
# /dev/input/event0 -> original signed Alpine evtest on immutable NVMe ext4.
# The independent QMP oracle physically injects KEY_A down+up and checks
# original program EV_KEY records, distinct SYN reports and ioctl state.
set -eu

out=build/h12-qemu-only
image="$out/naseed.iso"
serial_log="$out/h12-qemu-uefi-serial.log"
clean="$out/h12-qemu-uefi-clean.log"
oracle_log="$out/h12-qmp-evtest.log"
qmp_sock="$PWD/$out/h12-qmp.sock"
for path in "$image" "$out/h12-alpine.ext4" \
            "$out/h12-media-before.sha256" \
            "$out/h12-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H12 QEMU FAIL] missing/symlinked original media/boot artifact $path" >&2
        exit 2
    }
done
[ ! -e "$qmp_sock" ] && [ ! -L "$qmp_sock" ] &&
[ ! -L "$serial_log" ] && [ ! -L "$oracle_log" ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] || {
    echo '[H12 QEMU FAIL] stale QMP socket, log symlink or ambient VirtIO input/block' >&2
    exit 2
}
(cd "$out" && sha256sum -c h12-media-before.sha256)
sha256sum -c "$out/h12-original-alpine-elfs.sha256"

qemu_pid=''
oracle_pid=''
watchdog_pid=''
trap 'for child in "$watchdog_pid" "$oracle_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
python3 scripts/h12-qmp-evtest.py --serial "$serial_log" \
    --qmp "$qmp_sock" --timeout 270 > "$oracle_log" 2>&1 &
oracle_pid=$!
rc=0
NAS_QEMU_H12_USB_KBD_POSITIVE=1 NAS_QEMU_QMP_SOCKET="$qmp_sock" \
    timeout --signal=TERM --kill-after=3s 360s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
# Fail fast without treating a hand-written marker as positive proof. A
# failing original evtest/QMP oracle or real fatal guest serial stops this
# single QEMU process; the real PID1=0 exit and program events are still
# checked independently below. Keep wait in foreground to reap zombies.
(
    while :; do
        if grep -Eq '^\[(H12 HID FAIL|H12 XHCI FAIL|H12 USB FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)' "$serial_log" ||
           grep -Fq '[H12 QMP REAL USB HID FAIL]' "$oracle_log"; then
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
    echo "[H12 QEMU FAIL] genuine original evtest PID1 did not exit 0 rc=1, observed $rc" >&2
    tail -200 "$clean" >&2
    tail -80 "$oracle_log" >&2
    exit 1
fi
oracle_rc=0
wait "$oracle_pid" || oracle_rc=$?
oracle_pid=''
[ "$oracle_rc" -eq 0 ] &&
grep -Fq '[H12 QMP REAL USB HID EVTEST PASS]' "$oracle_log" || {
    echo '[H12 QEMU FAIL] independent QMP physical A key and original evtest oracle rejected run' >&2
    tail -110 "$oracle_log" >&2
    tail -160 "$clean" >&2
    exit 1
}

for required in \
    '[H12 HID BAR QUALIFIED]' \
    '[H12 HID USB ENUMERATED]' \
    '[H12 HID INPUT ATTACHED] /dev/input/event0' \
    '[H12 HID BOOT CONTINUE]' \
    '[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    '[EXEC] replaced process with lwext4 ELF: /usr/bin/evtest' \
    'H12_HID_READY_FOR_QMP' \
    'H12_HID_A_DOWN_WITH_SYN' \
    'H12_HID_A_UP_WITH_SYN' \
    'H12_ORIGINAL_ALPINE_EVTEST_USB_PASS' \
    '[H12 HID QUIESCED RELEASED]' \
    '[H12 HID USERLAND SHUTDOWN]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H12 QEMU FAIL] missing real USB HID/original software evidence: $required" >&2
        tail -200 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[H12 HID FAIL|\[H12 XHCI FAIL|H12_HID_DOWN_STATE_WRONG|H12_HID_EVTEST_STREAM_ENDED_BEFORE_PASS' "$clean" ||
   grep -Eq '\[VIRTIO-INPUT\]|\[VIRTIO-BLK\]|\[H9 AHCI|\[H11 USB BLOCK' "$clean"; then
    echo '[H12 QEMU FAIL] fatal userland/kernel or an unauthorized input/block device fallback' >&2
    tail -180 "$clean" >&2
    exit 1
fi
(cd "$out" && sha256sum -c h12-media-before.sha256)
sha256sum -c "$out/h12-original-alpine-elfs.sha256"
echo '[H12 REAL USB HID ALPINE EVTEST PASS] QEMU xHCI usb-kbd physical A down/up -> real EV_KEY(30) 1,0 + SYN and EVIOCGKEY via original signed Alpine evtest; PID1=0, DMA/BAR released, NVMe SHA unchanged'
tail -130 "$clean"
tail -30 "$oracle_log"
