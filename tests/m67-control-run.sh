#!/bin/sh
# Original signed Alpine procps-ng pgrep/pkill: target real live processes,
# preserve POSIX signal/wait status and discover simultaneous failure classes.
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
failures=0
a=0 b=0
if /bin/busybox cat < /dev/null > /tmp/m67-null; then
    echo M67_STD_DEVICE_NULL_PASS
else
    echo "[M67 FAIL] standard /dev/null open for shell redirection" >&2
    failures=$((failures+1))
fi
/bin/busybox sleep 137 &
a=$!
/bin/busybox sleep 139 &
b=$!
/bin/busybox sleep 0.1
echo "[M67] live worker PIDs: $a $b"
if test "$a" -gt 1 && test "$b" -gt "$a" &&
   /bin/busybox cat "/proc/$a/cmdline" > /tmp/m67-argv-a &&
   /bin/busybox cat "/proc/$b/cmdline" > /tmp/m67-argv-b &&
   test -s /tmp/m67-argv-a && test -s /tmp/m67-argv-b &&
   /usr/bin/pgrep -f 'busybox sleep 137' > /tmp/m67-a &&
   /usr/bin/pgrep -f 'busybox sleep 139' > /tmp/m67-b &&
   /bin/busybox grep -Fxq "$a" /tmp/m67-a &&
   /bin/busybox grep -Fxq "$b" /tmp/m67-b; then
    echo M67_ORIGINAL_PGREP_CMDLINE_PASS
else
    echo "[M67 FAIL] actual argv+procfs PID lookup" >&2
    failures=$((failures+1))
fi
if /usr/bin/pgrep -x busybox > /tmp/m67-comm &&
   /bin/busybox grep -Fxq "$a" /tmp/m67-comm &&
   /bin/busybox grep -Fxq "$b" /tmp/m67-comm; then
    echo M67_ORIGINAL_PGREP_COMM_PASS
else
    echo "[M67 FAIL] exact comm matching" >&2
    failures=$((failures+1))
fi
if /usr/bin/pkill -STOP -f 'busybox sleep 137' &&
   /bin/busybox grep -Eq '^State:[[:space:]]*T' "/proc/$a/status" &&
   /usr/bin/pkill -CONT -f 'busybox sleep 137' &&
   /bin/busybox grep -Eq '^State:[[:space:]]*[SR]' "/proc/$a/status"; then
    echo M67_ORIGINAL_PKILL_STOP_CONT_PASS
else
    echo "[M67 FAIL] exact process STOP/CONT and proc status" >&2
    failures=$((failures+1))
    /bin/busybox kill -CONT "$a" 2>/dev/null || :
fi
if /usr/bin/pkill -TERM -f 'busybox sleep 137'; then
    wait "$a"
    status=$?
    if test "$status" -eq 143 &&
       /bin/busybox kill -0 "$b" 2>/dev/null; then
        echo M67_ORIGINAL_PKILL_TERM_WAIT_PASS
    else
        echo "[M67 FAIL] signal wait status=$status or collateral kill" >&2
        failures=$((failures+1))
    fi
else
    echo "[M67 FAIL] pkill -TERM" >&2
    failures=$((failures+1))
fi
if /usr/bin/pkill -KILL -f 'busybox sleep 139'; then
    wait "$b"
    status=$?
    if test "$status" -eq 137; then
        echo M67_ORIGINAL_PKILL_KILL_WAIT_PASS
    else
        echo "[M67 FAIL] SIGKILL wait status=$status" >&2
        failures=$((failures+1))
    fi
else
    echo "[M67 FAIL] pkill -KILL" >&2
    failures=$((failures+1))
fi
/bin/busybox kill -CONT "$a" "$b" 2>/dev/null || :
/bin/busybox kill -KILL "$a" "$b" 2>/dev/null || :
wait "$a" 2>/dev/null || :
wait "$b" 2>/dev/null || :
if /usr/bin/pgrep -f 'busybox sleep 137' >/tmp/m67-negative 2>&1; then
    echo "[M67 FAIL] terminated process still visible" >&2
    failures=$((failures+1))
else
    echo M67_ORIGINAL_PGREP_REAPED_PASS
fi
echo "[M67 SUMMARY] independent failures=$failures"
for f in /tmp/m67-a /tmp/m67-b /tmp/m67-comm /tmp/m67-negative; do
    if test -f "$f"; then
        echo "[M67 DIAG] $f"
        /bin/busybox head -c 1500 "$f" || :
        echo
    fi
done
test "$failures" -eq 0 || exit 1
echo M67_PROCPS_CONTROL_FULL_PASS
