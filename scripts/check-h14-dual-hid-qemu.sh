#!/bin/sh
# REAL QEMU: one xHCI with simultaneous Boot Keyboard + Mouse root ports -> event0/1
# /dev/input/event0 then event1 -> original signed Alpine evtest + BusyBox/Lua/jq/SQLite.
# The independent QMP oracle injects KEY_A and mouse events while both devices are live;
# only real original evtest EV_KEY/EV_REL/SYN and ioctl state can pass.
set -eu

out=build/h14-qemu-only
image="$out/naseed.iso"
serial_log="$out/h14-qemu-uefi-serial.log"
clean="$out/h14-qemu-uefi-clean.log"
oracle_log="$out/h14-qmp-evtest.log"
qmp_sock="$PWD/$out/h14-qmp.sock"
for path in "$image" "$out/h14-alpine.ext4" \
            "$out/h14-media-before.sha256" \
            "$out/h14-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H14 QEMU FAIL] missing/symlinked original media/boot artifact $path" >&2
        exit 2
    }
done
[ ! -e "$qmp_sock" ] && [ ! -L "$qmp_sock" ] &&
[ ! -L "$serial_log" ] && [ ! -L "$oracle_log" ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] || {
    echo '[H14 QEMU FAIL] stale QMP socket, log symlink or ambient VirtIO input/block' >&2
    exit 2
}
(cd "$out" && sha256sum -c h14-media-before.sha256)
sha256sum -c "$out/h14-original-alpine-elfs.sha256"

qemu_pid=''
oracle_pid=''
watchdog_pid=''
trap 'for child in "$watchdog_pid" "$oracle_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
python3 scripts/h14-qmp-dual-evtest.py --serial "$serial_log" \
    --qmp "$qmp_sock" --timeout 270 > "$oracle_log" 2>&1 &
oracle_pid=$!
rc=0
NAS_QEMU_H14_DUAL_HID_USERLAND=1 NAS_QEMU_QMP_SOCKET="$qmp_sock" \
    timeout --signal=TERM --kill-after=3s 360s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
# Fail fast without treating a hand-written marker as positive proof. A
# failing original evtest/QMP oracle or real fatal guest serial stops this
# single QEMU process; the real PID1=0 exit and program events are still
# checked independently below. Keep wait in foreground to reap zombies.
(
    while :; do
        if grep -Eq '^\[(H14 DUAL FAIL|H14 XHCI FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)' "$serial_log" ||
           grep -Fq '[H14 QMP REAL DUAL USB EVTEST FAIL]' "$oracle_log"; then
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
    echo "[H14 QEMU FAIL] genuine original evtest PID1 did not exit 0 rc=1, observed $rc" >&2
    tail -200 "$clean" >&2
    tail -80 "$oracle_log" >&2
    exit 1
fi
oracle_rc=0
wait "$oracle_pid" || oracle_rc=$?
oracle_pid=''
[ "$oracle_rc" -eq 0 ] &&
grep -Fq '[H14 QMP REAL DUAL USB EVTEST PASS]' "$oracle_log" || {
    echo '[H14 QEMU FAIL] independent QMP dual USB keyboard+mouse original Alpine evtest oracle rejected run' >&2
    tail -110 "$oracle_log" >&2
    tail -160 "$clean" >&2
    exit 1
}

for required in \
    '[H14 DUAL BAR QUALIFIED]' \
    '[H14 DUAL KEYBOARD ATTACHED] /dev/input/event0 QEMU USB HID Boot Keyboard BUS_USB' \
    '[H14 DUAL MOUSE ATTACHED] /dev/input/event1 QEMU USB HID Boot Mouse BUS_USB' \
    '[H14 DUAL HID BOOT CONTINUE]' \
    '[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    '[EXEC] replaced process with lwext4 ELF: /usr/bin/evtest' \
    'H14_KBD_READY_FOR_QMP' \
    'H14_KBD_A_DOWN_WITH_SYN' \
    'H14_KBD_A_UP_WITH_SYN' \
    'H14_MOUSE_READY_FOR_QMP' \
    'H14_DUAL_READY_FOR_QMP' \
    'H14_MOUSE_REL_X_WITH_SYN' \
    'H14_MOUSE_REL_Y_WITH_SYN' \
    'H14_MOUSE_BTN_DOWN_WITH_SYN' \
    'H14_MOUSE_BTN_UP_WITH_SYN' \
    'H14_ALPINE_BUSYBOX_NVME_PASS' \
    'H14_ALPINE_LUA54_NVME_PASS' \
    'H14_ALPINE_JQ_NVME_PASS' \
    'H14_ALPINE_SQLITE_NVME_PASS' \
    'H14_ALPINE_READONLY_NVME_PASS' \
    'H14_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS' \
    'H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS' \
    '[H14 DUAL QUIESCED RELEASED]' \
    '[H14 DUAL USERLAND SHUTDOWN]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    if ! grep -Fq "$required" "$clean"; then
        echo "[H14 QEMU FAIL] missing real USB HID/original software evidence: $required" >&2
        tail -200 "$clean" >&2
        exit 1
    fi
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[H14 DUAL FAIL|\[H14 XHCI FAIL|H14_DUAL_EVTEST_FAIL_STAGE_' "$clean" ||
   grep -Eq '\[VIRTIO-INPUT\]|\[VIRTIO-BLK\]|\[H9 AHCI|\[H11 USB BLOCK' "$clean"; then
    echo '[H14 QEMU FAIL] fatal userland/kernel or an unauthorized input/block device fallback' >&2
    tail -180 "$clean" >&2
    exit 1
fi
[ "$(grep -Fc '[EXEC] replaced process with lwext4 ELF: /usr/bin/evtest' "$clean")" -ge 2 ] || {
    echo '[H14 QEMU FAIL] original signed Alpine evtest did not execute separately on both USB devices' >&2
    exit 1
}
[ "$(grep -Fc 'Input device ID: bus 0x3' "$clean")" -ge 2 ] || {
    echo '[H14 QEMU FAIL] original evtest did not show BUS_USB for both keyboard and mouse' >&2
    exit 1
}
(cd "$out" && sha256sum -c h14-media-before.sha256)
sha256sum -c "$out/h14-original-alpine-elfs.sha256"
echo '[H14 REAL DUAL USB ALPINE EVTEST PASS] one xHCI two root-port devices, two concurrent original signed Alpine evtest processes event0 KEY_A 1/0 and event1 REL_X/Y BTN_LEFT 1/0 with interleaved QMP, BusyBox/Lua/jq/SQLite, per-frame SYN, PID1=0, DMA/BAR released, media SHA unchanged'
tail -130 "$clean"
tail -30 "$oracle_log"
