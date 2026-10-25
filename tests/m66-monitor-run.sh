#!/bin/sh
# Probe every original package independently inside NaSeed. No fake PASS:
# print guest output for each failure, then report every distinct blocker.
PATH=/usr/bin:/bin
TERM=xterm
HOME=/tmp
LC_ALL=C
export PATH TERM HOME LC_ALL
failed=0
if /bin/busybox cat /proc/stat > /tmp/m66-stat &&
   /bin/busybox cat /proc/loadavg > /tmp/m66-load &&
   /bin/busybox cat /proc/1/statm > /tmp/m66-statm &&
   /bin/busybox grep -Eq '^cpu[[:space:]]' /tmp/m66-stat; then
 echo M66_PROCFS_METRICS_PASS
else
 echo '[M66 METRICS FAIL] procfs prerequisites' >&2
 failed=$((failed+1))
fi
if /usr/bin/top -b -n 1 > /tmp/m66-top 2>&1 &&
   /bin/busybox grep -Eq 'PID|USER' /tmp/m66-top &&
   /bin/busybox grep -q '1 ' /tmp/m66-top; then
 echo M66_ORIGINAL_TOP_PASS
else
 echo '[M66 TOP FAIL] original top batch' >&2
 /bin/busybox head -c 2400 /tmp/m66-top 2>/dev/null || :
 failed=$((failed+1))
fi
if /usr/bin/uptime > /tmp/m66-uptime 2>&1; then
 echo M66_ORIGINAL_UPTIME_PASS
else
 echo '[M66 UPTIME FAIL]' >&2
 /bin/busybox cat /tmp/m66-uptime || :
 failed=$((failed+1))
fi
if /usr/bin/htop --version > /tmp/m66-htop-version 2>&1 &&
   /bin/busybox grep -qi 'htop' /tmp/m66-htop-version; then
 echo M66_ORIGINAL_HTOP_ELF_PASS
else
 echo '[M66 HTOP ELF FAIL]' >&2
 /bin/busybox cat /tmp/m66-htop-version || :
 failed=$((failed+1))
fi
# Genuine interactive htop inside a tmux-backed PTY. Keep the child
# shell in a separate stage script so variables are not expanded by an outer
# shell and do not mistake an empty status for a clean program exit.
TMUX_TMPDIR=/tmp
export TMUX_TMPDIR
/bin/busybox mkdir -p /tmp/m66-tmux
if /usr/bin/tmux -L m66 -f /dev/null new-session -d -s m66 \
     '/bin/sh /tests/m66-htop-pty.sh'; then
 echo '[M66 PTY] tmux server and child started'
 /bin/busybox sleep 1
 if /usr/bin/tmux -L m66 capture-pane -p -t m66:0.0 > /tmp/m66-capture; then
   echo '[M66 PTY] terminal capture acquired'
   if /bin/busybox grep -Eq 'Tasks|CPU|Mem|PID|Load average|Uptime' /tmp/m66-capture; then
      echo '[M66 PTY] actual htop interface drawn'
      if /usr/bin/tmux -L m66 send-keys -t m66:0.0 q; then
         echo '[M66 PTY] q delivered'
         for attempt in 1 2 3 4 5 6 7 8 9 10 11 12; do
           test -s /tmp/m66-tmux/htop-status && break
           /bin/busybox sleep 0.2
         done
         if test -f /tmp/m66-tmux/exited &&
            test "$(/bin/busybox cat /tmp/m66-tmux/htop-status)" = 0; then
             echo M66_ORIGINAL_HTOP_PTY_PASS
         else
             echo '[M66 HTOP FAIL] exited marker absent or nonzero' >&2
             failed=$((failed+1))
         fi
      else
         echo '[M66 HTOP FAIL] could not send q into PTY' >&2
         failed=$((failed+1))
      fi
   else
      echo '[M66 HTOP FAIL] curses screen absent' >&2
      failed=$((failed+1))
   fi
 else
   echo '[M66 HTOP FAIL] tmux pane capture failed' >&2
   failed=$((failed+1))
 fi
else
 echo '[M66 HTOP FAIL] tmux server launch failed' >&2
 failed=$((failed+1))
fi
/usr/bin/tmux -L m66 kill-server 2>/dev/null || :
echo "[M66 SUMMARY] independent failed=$failed"
for filename in /tmp/m66-top /tmp/m66-uptime /tmp/m66-capture \
                /tmp/m66-tmux/htop-stderr /tmp/m66-tmux/htop-status; do
  if test -s "$filename"; then
    echo "[M66 DIAG] $filename:"
    /bin/busybox head -c 1100 "$filename" || :
    echo
  fi
done
test "$failed" -eq 0 || exit 1
echo M66_MONITOR_FULL_PASS
