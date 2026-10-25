#!/bin/sh
set -eu
test -x /usr/bin/tmux
/usr/bin/tmux -V > /tmp/m60-tmux-version
/bin/busybox grep -Fq 'tmux ' /tmp/m60-tmux-version
echo M60_ORIGINAL_TMUX_PASS

# The real server MUST allocate a real PTY and run a real shell inside it.
# The file is produced inside that child, not by the enclosing test script.
# tmux performs AF_UNIX IPC and uses posix_openpt/grantpt/unlockpt/ptsname.
TMUX_TMPDIR=/tmp
export TMUX_TMPDIR
/bin/busybox mkdir -p /tmp/m60-tmux
/usr/bin/tmux -L m60-os -f /dev/null new-session -d -s m60 \
    'sh -c "printf tmux-child-real-pty > /tmp/m60-tmux/child; /bin/busybox sleep 4"'
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    test -s /tmp/m60-tmux/child && break
    /bin/busybox sleep 0.1
done
test "$(/bin/busybox cat /tmp/m60-tmux/child)" = tmux-child-real-pty
/usr/bin/tmux -L m60-os list-sessions > /tmp/m60-tmux/sessions
/bin/busybox grep -Fq 'm60:' /tmp/m60-tmux/sessions
/usr/bin/tmux -L m60-os kill-server
echo M60_TMUX_REAL_PTY_CHILD_PASS
