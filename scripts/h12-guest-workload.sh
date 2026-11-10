#!/bin/sh
# H12 physical xHCI USB keyboard: ORIGINAL signed Alpine evtest observes
# /dev/input/event0 while the QMP oracle sends real A-down/A-up HID reports.
# Root NVMe ext4 is readonly; /tmp is a separate writable NaSeed RAM overlay.
set -eu

test -x /usr/bin/evtest
test -c /dev/input/event0

# Upstream evtest calls setbuf(stdout,NULL). Every line in this RAM log is
# written by the original executable; the script NEVER fabricates input.
h12_log="/tmp/h12-original-evtest-$$.log"
: > "$h12_log"
h12_evtest_pid=''
h12_cleanup() {
    if test -n "$h12_evtest_pid"; then
        kill "$h12_evtest_pid" 2>/dev/null || :
        wait "$h12_evtest_pid" 2>/dev/null || :
    fi
    /bin/busybox rm -f "$h12_log" || :
}
trap h12_cleanup EXIT
trap 'exit 1' HUP INT TERM

/usr/bin/evtest /dev/input/event0 > "$h12_log" 2>&1 &
h12_evtest_pid=$!

# Every stage is bounded independently; QMP imposes a host wall-clock bound.
h12_wait_for() {
    h12_stage=$1
    h12_count=0
    while test "$h12_count" -lt 300; do
        if test "$h12_stage" = ready; then
            if /bin/busybox grep -Fq 'Event code 30 (KEY_A)' "$h12_log" &&
               /bin/busybox grep -Fq 'Testing ... (interrupt to exit)' "$h12_log"; then
                return 0
            fi
        elif test "$h12_stage" = down; then
            if /bin/busybox grep -Fq 'type 1 (EV_KEY), code 30 (KEY_A), value 1' "$h12_log" &&
               test "$(/bin/busybox grep -c '^Event: time .*SYN_REPORT' "$h12_log" || :)" -ge 1; then
                return 0
            fi
        elif test "$h12_stage" = up; then
            if /bin/busybox grep -Fq 'type 1 (EV_KEY), code 30 (KEY_A), value 0' "$h12_log" &&
               test "$(/bin/busybox grep -c '^Event: time .*SYN_REPORT' "$h12_log" || :)" -ge 2; then
                return 0
            fi
        else
            return 1
        fi
        kill -0 "$h12_evtest_pid" 2>/dev/null || return 1
        h12_count=$((h12_count + 1))
        /bin/busybox sleep 1
    done
    return 1
}

if ! h12_wait_for ready; then
    printf '%s\n' H12_HID_EVTEST_NOT_READY
    exit 1
fi
printf '%s\n' H12_HID_READY_FOR_QMP

# QMP must hold A down until this real EVIOCGKEY query observes depressed.
if ! h12_wait_for down; then
    printf '%s\n' H12_HID_A_DOWN_TIMEOUT
    exit 1
fi
h12_down_line=$(/bin/busybox grep -F 'type 1 (EV_KEY), code 30 (KEY_A), value 1' "$h12_log" | /bin/busybox head -n 1)
h12_first_syn=$(/bin/busybox grep '^Event: time .*SYN_REPORT' "$h12_log" | /bin/busybox head -n 1)
test -n "$h12_down_line" && test -n "$h12_first_syn"
if /usr/bin/evtest --query /dev/input/event0 EV_KEY KEY_A; then
    printf '%s\n' H12_HID_A_DOWN_QUERY_WRONG
    exit 1
else
    h12_query_rc=$?
    test "$h12_query_rc" -eq 10 || exit 1
fi
printf '%s\n' "$h12_down_line" "$h12_first_syn"
printf '%s\n' H12_HID_A_DOWN_WITH_SYN

if ! h12_wait_for up; then
    printf '%s\n' H12_HID_A_UP_TIMEOUT
    exit 1
fi
h12_up_line=$(/bin/busybox grep -F 'type 1 (EV_KEY), code 30 (KEY_A), value 0' "$h12_log" | /bin/busybox head -n 1)
h12_second_syn=$(/bin/busybox grep '^Event: time .*SYN_REPORT' "$h12_log" | /bin/busybox sed -n '2p')
test -n "$h12_up_line" && test -n "$h12_second_syn"
/usr/bin/evtest --query /dev/input/event0 EV_KEY KEY_A
printf '%s\n' "$h12_up_line" "$h12_second_syn"
printf '%s\n' H12_HID_A_UP_WITH_SYN

# SIGTERM's nonzero wait status is deliberately swallowed by cleanup;
# PID1 exits zero only after both original evtest frames and queries passed.
h12_cleanup
h12_evtest_pid=''
printf '%s\n' H12_ORIGINAL_ALPINE_EVTEST_USB_PASS
