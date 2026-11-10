#!/bin/sh
# H13 genuine USB HID Boot Mouse -> /dev/input/event0 -> ORIGINAL
# signed Alpine evtest; QMP injects real relative X/Y and BTN_LEFT down/up.
# The signed NVMe ext4 root remains read-only; log is volatile RAM /tmp.
set -eu
test -x /usr/bin/evtest
test -c /dev/input/event0

h13_log="/tmp/h13-original-evtest-$$.log"
: > "$h13_log"
h13_evtest_pid=''
h13_cleanup() {
    if test -n "$h13_evtest_pid"; then
        kill "$h13_evtest_pid" 2>/dev/null || :
        wait "$h13_evtest_pid" 2>/dev/null || :
    fi
    /bin/busybox rm -f "$h13_log" || :
}
trap h13_cleanup EXIT
trap 'exit 1' HUP INT TERM
/usr/bin/evtest /dev/input/event0 > "$h13_log" 2>&1 &
h13_evtest_pid=$!

h13_wait_for() {
    h13_stage=$1
    h13_count=0
    while test "$h13_count" -lt 300; do
        if test "$h13_stage" = ready; then
            if /bin/busybox grep -Fq 'Input device ID: bus 0x3' "$h13_log" &&
               /bin/busybox grep -Fq 'Input device name: "QEMU USB HID Boot Mouse"' "$h13_log" &&
               /bin/busybox grep -Fq 'Event code 0 (REL_X)' "$h13_log" &&
               /bin/busybox grep -Fq 'Event code 1 (REL_Y)' "$h13_log" &&
               /bin/busybox grep -Fq 'Event code 272 (BTN_LEFT)' "$h13_log" &&
               /bin/busybox grep -Fq 'Testing ... (interrupt to exit)' "$h13_log"; then
                return 0
            fi
        elif test "$h13_stage" = x; then
            /bin/busybox grep -Eq 'type 2 \(EV_REL\), code 0 \(REL_X\), value -?[1-9][0-9]*' "$h13_log" && return 0
        elif test "$h13_stage" = y; then
            /bin/busybox grep -Eq 'type 2 \(EV_REL\), code 1 \(REL_Y\), value -?[1-9][0-9]*' "$h13_log" && return 0
        elif test "$h13_stage" = down; then
            /bin/busybox grep -Fq 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 1' "$h13_log" && return 0
        elif test "$h13_stage" = up; then
            /bin/busybox grep -Fq 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 0' "$h13_log" && return 0
        else
            return 1
        fi
        kill -0 "$h13_evtest_pid" 2>/dev/null || return 1
        h13_count=$((h13_count+1))
        /bin/busybox sleep 1
    done
    return 1
}
h13_fail() {
    printf '%s\n' "H13_MOUSE_EVTEST_FAIL_STAGE_$1"
    /bin/busybox cat "$h13_log" || :
    exit 1
}
# The original signed evtest must validate physical BUS_USB identity, name
# and actual device event capabilities; PS/2 (BUS_I8042) cannot masquerade.
h13_wait_for ready || h13_fail READY
# Preserve the exact original evtest EVIOCGID/EVIOCGNAME output in the real
# serial transcript so the independent QMP oracle can reject PS/2 routing.
/bin/busybox grep -F 'Input device ID:' "$h13_log"
/bin/busybox grep -F 'Input device name:' "$h13_log"
printf '%s\n' H13_MOUSE_READY_FOR_QMP

h13_wait_for x || h13_fail REL_X
h13_x_line=$(/bin/busybox grep -E 'type 2 \(EV_REL\), code 0 \(REL_X\), value -?[1-9][0-9]*' "$h13_log" | /bin/busybox head -n 1)
h13_x_syn=$(/bin/busybox grep 'SYN_REPORT' "$h13_log" | /bin/busybox head -n 1)
test -n "$h13_x_line" && test -n "$h13_x_syn" || h13_fail X_SYN
printf '%s\n' "$h13_x_line" "$h13_x_syn" H13_MOUSE_REL_X_WITH_SYN

h13_wait_for y || h13_fail REL_Y
h13_y_line=$(/bin/busybox grep -E 'type 2 \(EV_REL\), code 1 \(REL_Y\), value -?[1-9][0-9]*' "$h13_log" | /bin/busybox head -n 1)
h13_y_syn=$(/bin/busybox grep 'SYN_REPORT' "$h13_log" | /bin/busybox sed -n '2p')
test -n "$h13_y_line" && test -n "$h13_y_syn" || h13_fail Y_SYN
printf '%s\n' "$h13_y_line" "$h13_y_syn" H13_MOUSE_REL_Y_WITH_SYN

h13_wait_for down || h13_fail BTN_DOWN
h13_down_line=$(/bin/busybox grep -F 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 1' "$h13_log" | /bin/busybox head -n 1)
h13_down_syn=$(/bin/busybox grep 'SYN_REPORT' "$h13_log" | /bin/busybox sed -n '3p')
test -n "$h13_down_line" && test -n "$h13_down_syn" || h13_fail DOWN_SYN
if /usr/bin/evtest --query /dev/input/event0 EV_KEY BTN_LEFT; then
    h13_fail BTN_DOWN_QUERY_NOT_HELD
else
    h13_query_rc=$?
    test "$h13_query_rc" -eq 10 || h13_fail BTN_DOWN_QUERY_ERROR
fi
printf '%s\n' "$h13_down_line" "$h13_down_syn" H13_MOUSE_BTN_DOWN_WITH_SYN

h13_wait_for up || h13_fail BTN_UP
h13_up_line=$(/bin/busybox grep -F 'type 1 (EV_KEY), code 272 (BTN_LEFT), value 0' "$h13_log" | /bin/busybox head -n 1)
h13_up_syn=$(/bin/busybox grep 'SYN_REPORT' "$h13_log" | /bin/busybox sed -n '4p')
test -n "$h13_up_line" && test -n "$h13_up_syn" || h13_fail UP_SYN
/usr/bin/evtest --query /dev/input/event0 EV_KEY BTN_LEFT || h13_fail BTN_UP_QUERY_STUCK
printf '%s\n' "$h13_up_line" "$h13_up_syn" H13_MOUSE_BTN_UP_WITH_SYN
h13_cleanup
h13_evtest_pid=''
printf '%s\n' H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS
