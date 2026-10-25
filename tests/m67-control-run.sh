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
 echo "[M67 FAIL] /dev/null" >&2;failures=$((failures+1))
fi
# Use real pre-existing M60 tmux PTYs as background supervisors. This runs
# two genuine original BusyBox sleeps concurrently, without relying on
# ash's as-yet-unverified noninteractive '&' /dev/null path.
TMUX_TMPDIR=/tmp
TERM=xterm
export TERM TMUX_TMPDIR
if /usr/bin/tmux -L m67 -f /dev/null new-session -d -s m67a \
    '/bin/sh /tests/m67-worker.sh 137 a' &&
   /usr/bin/tmux -L m67 -f /dev/null new-session -d -s m67b \
    '/bin/sh /tests/m67-worker.sh 139 b'; then
  echo M67_REAL_TMUX_PTYS_PASS
else
  echo '[M67 FAIL] M60 original tmux process supervisor' >&2
  failures=$((failures+1))
fi
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
 /usr/bin/pgrep -f -x '/bin/busybox sleep 137' > /tmp/m67-a 2>/dev/null || :
 /usr/bin/pgrep -f -x '/bin/busybox sleep 139' > /tmp/m67-b 2>/dev/null || :
 test -s /tmp/m67-a && test -s /tmp/m67-b && break
 /bin/busybox sleep 0.1
done
a=$(/bin/busybox head -n 1 /tmp/m67-a)
b=$(/bin/busybox head -n 1 /tmp/m67-b)
echo "[M67] two real target PIDs $a $b"
if test -n "$a" && test -n "$b" && test "$a" -ne "$b" &&
   /bin/busybox cat "/proc/$a/cmdline" > /tmp/m67-argv-a &&
   /bin/busybox cat "/proc/$b/cmdline" > /tmp/m67-argv-b &&
   test -s /tmp/m67-argv-a && test -s /tmp/m67-argv-b &&
   /bin/busybox grep -Fxq "$a" /tmp/m67-a &&
   /bin/busybox grep -Fxq "$b" /tmp/m67-b; then
 echo M67_ORIGINAL_PGREP_CMDLINE_PASS
else
 echo "[M67 FAIL] two live PID-specific /proc cmdlines" >&2
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
if /usr/bin/pkill -TERM -f -x '/bin/busybox sleep 137'; then
 for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
   test -s /tmp/m67-a.status && break
   /bin/busybox sleep 0.2
 done
 status=$(/bin/busybox cat /tmp/m67-a.status 2>/dev/null)
 if test "$status" = 143 && /bin/busybox kill -0 "$b" 2>/dev/null; then
  echo M67_ORIGINAL_PKILL_TERM_WAIT_PASS
 else
  echo "[M67 FAIL] TERM child wait status=$status or peer killed" >&2
  failures=$((failures+1))
 fi
else
 echo '[M67 FAIL] pkill TERM target' >&2
 failures=$((failures+1))
fi
if /usr/bin/pkill -KILL -f -x '/bin/busybox sleep 139'; then
 for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
  test -s /tmp/m67-b.status && break
  /bin/busybox sleep 0.2
 done
 status=$(/bin/busybox cat /tmp/m67-b.status 2>/dev/null)
 if test "$status" = 137; then
  echo M67_ORIGINAL_PKILL_KILL_WAIT_PASS
 else
  echo "[M67 FAIL] KILL child wait status=$status" >&2
  failures=$((failures+1))
 fi
else
 echo '[M67 FAIL] pkill KILL target' >&2
 failures=$((failures+1))
fi
/usr/bin/tmux -L m67 kill-server 2>/dev/null || :
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
