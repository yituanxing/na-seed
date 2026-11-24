#!/bin/sh
# Shared real xHCI/QMP input transport; exact keyboard, mouse and dual proof stays separate.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
case "$profile" in
    h12) oracle_script=scripts/h12-qmp-evtest.py; positive_flag=NAS_QEMU_H12_USB_KBD_POSITIVE;
        fatal='^\[(H12 HID FAIL|H12 XHCI FAIL|H12 USB FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)'; oracle_fail='[H12 QMP REAL USB HID FAIL]';
        oracle_pass='[H12 QMP REAL USB HID EVTEST PASS]'; oracle_error='[H12 QEMU FAIL] independent QMP physical A key and original evtest oracle rejected run' ;;
    h13) oracle_script=scripts/h13-qmp-evtest.py; positive_flag=NAS_QEMU_H13_USB_MOUSE_USERLAND;
        fatal='^\[(H13 MOUSE FAIL|H13 XHCI FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)'; oracle_fail='[H13 QMP REAL USB MOUSE FAIL]';
        oracle_pass='[H13 QMP REAL USB MOUSE EVTEST PASS]'; oracle_error='[H13 QEMU FAIL] independent QMP physical USB mouse and original evtest oracle rejected run' ;;
    h14) oracle_script=scripts/h14-qmp-dual-evtest.py; positive_flag=NAS_QEMU_H14_DUAL_HID_USERLAND;
        fatal='^\[(H14 DUAL FAIL|H14 XHCI FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)'; oracle_fail='[H14 QMP REAL DUAL USB EVTEST FAIL]';
        oracle_pass='[H14 QMP REAL DUAL USB EVTEST PASS]'; oracle_error='[H14 QEMU FAIL] independent QMP dual USB keyboard+mouse original Alpine evtest oracle rejected run' ;;
    *) exit 2 ;;
esac
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
out=build/${profile}-qemu-only
image="$out/naseed.iso"
serial_log="$out/${profile}-qemu-uefi-serial.log"
clean="$out/${profile}-qemu-uefi-clean.log"
oracle_log="$out/${profile}-qmp-evtest.log"
qmp_sock="$PWD/$out/${profile}-qmp.sock"
for path in "$image" "$out/${profile}-alpine.ext4" \
            "$out/${profile}-media-before.sha256" \
            "$out/${profile}-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[${label} QEMU FAIL] missing/symlinked original media/boot artifact $path" >&2
        exit 2
    }
done
[ ! -e "$qmp_sock" ] && [ ! -L "$qmp_sock" ] &&
[ ! -L "$serial_log" ] && [ ! -L "$oracle_log" ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] || {
    echo "[${label} QEMU FAIL] stale QMP socket, log symlink or ambient VirtIO input/block" >&2
    exit 2
}
(cd "$out" && sha256sum -c ${profile}-media-before.sha256)
sha256sum -c "$out/${profile}-original-alpine-elfs.sha256"

qemu_pid=''
oracle_pid=''
watchdog_pid=''
trap 'for child in "$watchdog_pid" "$oracle_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
python3 "$oracle_script" --serial "$serial_log" \
    --qmp "$qmp_sock" --timeout 270 > "$oracle_log" 2>&1 &
oracle_pid=$!
rc=0
env "$positive_flag=1" NAS_QEMU_QMP_SOCKET="$qmp_sock" \
    timeout --signal=TERM --kill-after=3s 360s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
# Fail fast without treating a hand-written marker as positive proof. A
# failing original evtest/QMP oracle or real fatal guest serial stops this
# single QEMU process; the real PID1=0 exit and program events are still
# checked independently below. Keep wait in foreground to reap zombies.
(
    while :; do
        if grep -Eq "$fatal" "$serial_log" ||
           grep -Fq "$oracle_fail" "$oracle_log"; then
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
    echo "[${label} QEMU FAIL] genuine original evtest PID1 did not exit 0 rc=1, observed $rc" >&2
    tail -200 "$clean" >&2
    tail -80 "$oracle_log" >&2
    exit 1
fi
oracle_rc=0
wait "$oracle_pid" || oracle_rc=$?
oracle_pid=''
[ "$oracle_rc" -eq 0 ] &&
grep -Fq "$oracle_pass" "$oracle_log" || {
    echo "$oracle_error" >&2
    tail -110 "$oracle_log" >&2
    tail -160 "$clean" >&2
    exit 1
}

case "$profile" in
    h12)
        set -- \
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
            '[PASS] cmdline userspace exit 0'
        guest_fatal='\[FAIL\]|\[FAULT\]|\[H12 HID FAIL|\[H12 XHCI FAIL|H12_HID_DOWN_STATE_WRONG|H12_HID_EVTEST_STREAM_ENDED_BEFORE_PASS'
        success='[H12 REAL USB HID ALPINE EVTEST PASS] QEMU xHCI usb-kbd physical A down/up -> real EV_KEY(30) 1,0 + SYN and EVIOCGKEY via original signed Alpine evtest; PID1=0, DMA/BAR released, NVMe SHA unchanged'
        ;;
    h13)
        set -- \
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
            '[PASS] cmdline userspace exit 0'
        guest_fatal='\[FAIL\]|\[FAULT\]|\[H13 MOUSE FAIL|\[H13 XHCI FAIL|H13_MOUSE_EVTEST_FAIL_STAGE_'
        success='[H13 REAL USB MOUSE ALPINE EVTEST PASS] QEMU xHCI usb-mouse physical X/Y + BTN_LEFT(272) down/up -> real EV_REL, EV_KEY and SYN via original signed Alpine BUS_USB evtest; PID1=0, DMA/BAR released, NVMe SHA unchanged'
        ;;
    h14)
        set -- \
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
            '[PASS] cmdline userspace exit 0'
        guest_fatal='\[FAIL\]|\[FAULT\]|\[H14 DUAL FAIL|\[H14 XHCI FAIL|H14_DUAL_EVTEST_FAIL_STAGE_'
        success='[H14 REAL DUAL USB ALPINE EVTEST PASS] one xHCI two root-port devices, two concurrent original signed Alpine evtest processes event0 KEY_A 1/0 and event1 REL_X/Y BTN_LEFT 1/0 with interleaved QMP, BusyBox/Lua/jq/SQLite, per-frame SYN, PID1=0, DMA/BAR released, media SHA unchanged'
        ;;
esac
for required do
    grep -Fq "$required" "$clean" || {
        echo "[$label QEMU FAIL] missing real USB HID/original software evidence: $required" >&2
        tail -200 "$clean" >&2
        exit 1
    }
done
if grep -Eq "$guest_fatal" "$clean" ||
   grep -Eq '\[VIRTIO-INPUT\]|\[VIRTIO-BLK\]|\[H9 AHCI|\[H11 USB BLOCK' "$clean"; then
    echo "[$label QEMU FAIL] fatal userland/kernel or an unauthorized input/block device fallback" >&2
    tail -180 "$clean" >&2
    exit 1
fi
if [ "$profile" = h14 ]; then
    [ "$(grep -Fc '[EXEC] replaced process with lwext4 ELF: /usr/bin/evtest' "$clean")" -ge 2 ] || {
        echo '[H14 QEMU FAIL] original signed Alpine evtest did not execute separately on both USB devices' >&2
        exit 1
    }
    [ "$(grep -Fc 'Input device ID: bus 0x3' "$clean")" -ge 2 ] || {
        echo '[H14 QEMU FAIL] original evtest did not show BUS_USB for both keyboard and mouse' >&2
        exit 1
    }
fi
(cd "$out" && sha256sum -c "$profile-media-before.sha256")
sha256sum -c "$out/$profile-original-alpine-elfs.sha256"
echo "$success"
tail -130 "$clean"
tail -30 "$oracle_log"
