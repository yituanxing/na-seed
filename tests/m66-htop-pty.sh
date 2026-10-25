#!/bin/sh
# Runs on the genuine tmux PTY, launched by the full-system QEMU guest.
# Preserve the real htop exit status without nested-shell expansion.
LC_ALL=C TERM=xterm HOME=/tmp
export LC_ALL TERM HOME
/usr/bin/htop -d 10 2>/tmp/m66-tmux/htop-stderr
rc=$?
printf '%s\n' "$rc" > /tmp/m66-tmux/htop-status
if [ "$rc" -eq 0 ]; then
  echo htop-exited > /tmp/m66-tmux/exited
fi
/bin/busybox sleep 4
