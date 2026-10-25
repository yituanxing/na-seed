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
# Not an htop --version smoke: genuine curses app on the already-proven
# tmux PTY backend. Capture what it drew, send q, verify clean exit.
TMUX_TMPDIR=/tmp
export TMUX_TMPDIR
/bin/busybox mkdir -p /tmp/m66-tmux
if /usr/bin/tmux -L m66 -f /dev/null new-session -d -s m66 \
     'sh -c "htop -d 10; echo htop-exited > /tmp/m66-tmux/exited; sleep 2"' &&
   /bin/busybox sleep 2 &&
   /usr/bin/tmux -L m66 capture-pane -p -t m66:0.0 > /tmp/m66-capture &&
   /bin/busybox grep -Eq 'Tasks|CPU|Mem|PID|Load average' /tmp/m66-capture &&
   /usr/bin/tmux -L m66 send-keys -t m66:0.0 q &&
   /bin/busybox sleep 1 &&
   test -s /tmp/m66-tmux/exited; then
 echo M66_ORIGINAL_HTOP_PTY_PASS
else
 echo '[M66 HTOP PTY FAIL]' >&2
 /bin/busybox head -c 2200 /tmp/m66-capture 2>/dev/null || :
 failed=$((failed+1))
fi
/usr/bin/tmux -L m66 kill-server 2>/dev/null || :
test "$failed" -eq 0 || exit 1
echo M66_MONITOR_FULL_PASS
