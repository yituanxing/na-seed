#!/bin/sh
# Real foreground child owned by a real tmux PTY session. The parent shell
# performs a POSIX wait and persists its true WIFSIGNALED exit status.
tag=$2
/bin/busybox sleep "$1"
code=$?
echo "$code" > "/tmp/m67-$tag.status"
/bin/busybox sleep 5
