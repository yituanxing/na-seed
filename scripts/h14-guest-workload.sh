#!/bin/sh
# H14: one LIVE xHCI controller with keyboard and mouse on two root ports.
# Both USB devices exist together; ORIGINAL signed Alpine evtest inspects both
# real /dev/input/event0 and event1 in one PID1 boot. Hardware events come ONLY
# from QMP to real QEMU USB, not from this script or synthetic evdev writes.
set -eu
test -x /usr/bin/evtest
test -c /dev/input/event0
test -c /dev/input/event1
h14_kbd_log="/tmp/h14-original-evtest-kbd-$$.log"
h14_mouse_log="/tmp/h14-original-evtest-mouse-$$.log"
h14_kbd_pid=''
h14_mouse_pid=''
h14_cleanup() {
    for h14_pid in "$h14_kbd_pid" "$h14_mouse_pid"; do
        if test -n "$h14_pid"; then kill "$h14_pid" 2>/dev/null || :; fi
    done
    for h14_pid in "$h14_kbd_pid" "$h14_mouse_pid"; do
        if test -n "$h14_pid"; then wait "$h14_pid" 2>/dev/null || :; fi
    done
    h14_kbd_pid=''
    h14_mouse_pid=''
    /bin/busybox rm -f "$h14_kbd_log" "$h14_mouse_log" || :
}
trap h14_cleanup EXIT
trap 'exit 1' HUP INT TERM
h14_fail() {
    printf '%s\n' "H14_DUAL_EVTEST_FAIL_STAGE_$1"
    printf '%s\n' H14_KEYBOARD_ORIGINAL_EVTEST_LOG_BEGIN
    /bin/busybox cat "$h14_kbd_log" || :
    printf '%s\n' H14_MOUSE_ORIGINAL_EVTEST_LOG_BEGIN
    /bin/busybox cat "$h14_mouse_log" || :
    exit 1
}
h14_kbd_syn() {
    /bin/busybox grep '^Event: time .*SYN_REPORT' "$h14_kbd_log" | /bin/busybox sed -n "$1"
}
h14_mouse_syn() {
    /bin/busybox grep '^Event: time .*SYN_REPORT' "$h14_mouse_log" | /bin/busybox sed -n "$1"
}
h14_kbd_event() {
    /bin/busybox grep -F "$1" "$h14_kbd_log" | /bin/busybox head -n 1
}
h14_mouse_event() {
    /bin/busybox grep -F "$1" "$h14_mouse_log" | /bin/busybox head -n 1
}
h14_wait() {
    h14_condition=$1
    h14_count=0
    while test "$h14_count" -lt 300; do
        if test "$h14_condition" = dual_ready; then
            /bin/busybox grep -Fq 'Input device ID: bus 0x3' "$h14_kbd_log" &&
            /bin/busybox grep -Fq 'Input device name: "QEMU USB HID Boot Keyboard"' "$h14_kbd_log" &&
            /bin/busybox grep -Fq 'Event code 30 (KEY_A)' "$h14_kbd_log" &&
            /bin/busybox grep -Fq 'Testing ... (interrupt to exit)' "$h14_kbd_log" &&
            /bin/busybox grep -Fq 'Input device ID: bus 0x3' "$h14_mouse_log" &&
            /bin/busybox grep -Fq 'Input device name: "QEMU USB HID Boot Mouse"' "$h14_mouse_log" &&
            /bin/busybox grep -Fq 'Event code 0 (REL_X)' "$h14_mouse_log" &&
            /bin/busybox grep -Fq 'Event code 1 (REL_Y)' "$h14_mouse_log" &&
            /bin/busybox grep -Fq 'Event code 272 (BTN_LEFT)' "$h14_mouse_log" &&
            /bin/busybox grep -Fq 'Testing ... (interrupt to exit)' "$h14_mouse_log" && return 0
        elif test "$h14_condition" = kbd_down; then
            /bin/busybox grep -Fq 'type 1 (EV_KEY), code 30 (KEY_A), value 1' "$h14_kbd_log" &&
            test "$(/bin/busybox grep -c 'SYN_REPORT' "$h14_kbd_log" || :)" -ge 1 && return 0
        elif test "$h14_condition" = kbd_up; then
            /bin/busybox grep -Fq 'type 1 (EV_KEY), code 30 (KEY_A), value 0' "$h14_kbd_log" &&
            test "$(/bin/busybox grep -c 'SYN_REPORT' "$h14_kbd_log" || :)" -ge 2 && return 0
        elif test "$h14_condition" = mouse_x; then
            /bin/busybox grep -Eq 'type 2 \(EV_REL\), code 0 \(REL_X\), value -?[1-9][0-9]*' "$h14_mouse_log" &&
            test "$(/bin/busybox grep -c 'SYN_REPORT' "$h14_mouse_log" || :)" -ge 1 && return 0
        elif test "$h14_condition" = mouse_y; then
            /bin/busybox grep -Eq 'type 2 \(EV_REL\), code 1 \(REL_Y\), value -?[1-9][0-9]*' "$h14_mouse_log" &&
            test "$(/bin/busybox grep -c 'SYN_REPORT' "$h14_mouse_log" || :)" -ge 2 && return 0
        elif test "$h14_condition" = mouse_down; then
            /bin/busybox grep -Fq 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 1' "$h14_mouse_log" &&
            test "$(/bin/busybox grep -c 'SYN_REPORT' "$h14_mouse_log" || :)" -ge 3 && return 0
        elif test "$h14_condition" = mouse_up; then
            /bin/busybox grep -Fq 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 0' "$h14_mouse_log" &&
            test "$(/bin/busybox grep -c 'SYN_REPORT' "$h14_mouse_log" || :)" -ge 4 && return 0
        else
            return 1
        fi
        kill -0 "$h14_kbd_pid" 2>/dev/null || return 1
        kill -0 "$h14_mouse_pid" 2>/dev/null || return 1
        h14_count=$((h14_count+1))
        /bin/busybox sleep 1
    done
    return 1
}

# Re-run the previously proven real original software in this new H14 session.
# All ELF files, dynamic musl and signed evtest come from immutable NVMe ext4.
test "$(/bin/busybox cat /root/h14-disk-only.txt)" = \
    'H14_DUAL_SIGNED_ALPINE_DISK_ONLY_20261110'
/bin/busybox cmp -s /root/h14-disk-only.txt /root/h14-copy.txt
/bin/busybox echo H14_ALPINE_BUSYBOX_NVME_PASS
/usr/bin/lua5.4 -e 'assert(21 * 2 == 42); print("H14_ALPINE_LUA54_NVME_PASS")'
/usr/bin/jq -nr '"H14_ALPINE_JQ_NVME_PASS"'
/usr/bin/sqlite3 :memory: "SELECT 'H14_ALPINE_SQLITE_NVME_PASS';"
if /bin/busybox mkdir /root/h14-unexpected-write; then
    /bin/busybox echo H14_ALPINE_READONLY_VIOLATION
    exit 1
fi
test ! -e /root/h14-unexpected-write
/bin/busybox echo H14_ALPINE_READONLY_NVME_PASS
/bin/busybox echo H14_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS

# Both xHCI root-port devices were registered BEFORE signed BusyBox PID1.
# Keep both original signed evtest processes OPEN concurrently throughout all
# interleaved QMP key/mouse transitions to expose cross-slot demultiplex bugs.
: > "$h14_kbd_log"
: > "$h14_mouse_log"
/usr/bin/evtest /dev/input/event0 > "$h14_kbd_log" 2>&1 &
h14_kbd_pid=$!
/usr/bin/evtest /dev/input/event1 > "$h14_mouse_log" 2>&1 &
h14_mouse_pid=$!
h14_wait dual_ready || h14_fail DUAL_READY
/bin/busybox grep -F 'Input device ID:' "$h14_kbd_log"
/bin/busybox grep -F 'Input device name:' "$h14_kbd_log"
/bin/busybox grep -F 'Input device ID:' "$h14_mouse_log"
/bin/busybox grep -F 'Input device name:' "$h14_mouse_log"
printf '%s\n' H14_KBD_READY_FOR_QMP H14_MOUSE_READY_FOR_QMP H14_DUAL_READY_FOR_QMP

# Physical QMP interleave: KEY_A down, mouse X, KEY_A up, mouse Y,
# BTN_LEFT down and up. Both source evtest processes remain alive.
h14_wait kbd_down || h14_fail KEYBOARD_DOWN
h14_line=$(h14_kbd_event 'type 1 (EV_KEY), code 30 (KEY_A), value 1')
h14_syn=$(h14_kbd_syn '1p')
test -n "$h14_line" && test -n "$h14_syn" || h14_fail KEYBOARD_DOWN_SYN
if /usr/bin/evtest --query /dev/input/event0 EV_KEY KEY_A; then
    h14_fail KEYBOARD_DOWN_NOT_HELD
else
    h14_rc=$?
    test "$h14_rc" -eq 10 || h14_fail KEYBOARD_DOWN_QUERY_ERROR
fi
printf '%s\n' "$h14_line" "$h14_syn" H14_KBD_A_DOWN_WITH_SYN

h14_wait mouse_x || h14_fail MOUSE_X
h14_line=$(h14_mouse_event 'type 2 (EV_REL), code 0 (REL_X), value ')
h14_syn=$(h14_mouse_syn '1p')
test -n "$h14_line" && test -n "$h14_syn" || h14_fail MOUSE_X_SYN
printf '%s\n' "$h14_line" "$h14_syn" H14_MOUSE_REL_X_WITH_SYN

h14_wait kbd_up || h14_fail KEYBOARD_UP
h14_line=$(h14_kbd_event 'type 1 (EV_KEY), code 30 (KEY_A), value 0')
h14_syn=$(h14_kbd_syn '2p')
test -n "$h14_line" && test -n "$h14_syn" || h14_fail KEYBOARD_UP_SYN
/usr/bin/evtest --query /dev/input/event0 EV_KEY KEY_A || h14_fail KEYBOARD_UP_QUERY_STUCK
printf '%s\n' "$h14_line" "$h14_syn" H14_KBD_A_UP_WITH_SYN

h14_wait mouse_y || h14_fail MOUSE_Y
h14_line=$(h14_mouse_event 'type 2 (EV_REL), code 1 (REL_Y), value ')
h14_syn=$(h14_mouse_syn '2p')
test -n "$h14_line" && test -n "$h14_syn" || h14_fail MOUSE_Y_SYN
printf '%s\n' "$h14_line" "$h14_syn" H14_MOUSE_REL_Y_WITH_SYN

h14_wait mouse_down || h14_fail MOUSE_DOWN
h14_line=$(h14_mouse_event 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 1')
h14_syn=$(h14_mouse_syn '3p')
test -n "$h14_line" && test -n "$h14_syn" || h14_fail MOUSE_DOWN_SYN
if /usr/bin/evtest --query /dev/input/event1 EV_KEY BTN_LEFT; then
    h14_fail MOUSE_DOWN_NOT_HELD
else
    h14_rc=$?
    test "$h14_rc" -eq 10 || h14_fail MOUSE_DOWN_QUERY_ERROR
fi
printf '%s\n' "$h14_line" "$h14_syn" H14_MOUSE_BTN_DOWN_WITH_SYN

h14_wait mouse_up || h14_fail MOUSE_UP
h14_line=$(h14_mouse_event 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 0')
h14_syn=$(h14_mouse_syn '4p')
test -n "$h14_line" && test -n "$h14_syn" || h14_fail MOUSE_UP_SYN
/usr/bin/evtest --query /dev/input/event1 EV_KEY BTN_LEFT || h14_fail MOUSE_UP_QUERY_STUCK
printf '%s\n' "$h14_line" "$h14_syn" H14_MOUSE_BTN_UP_WITH_SYN
h14_cleanup
printf '%s\n' H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS
