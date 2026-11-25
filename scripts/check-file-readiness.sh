#!/bin/sh
set -eu
# No references to the retired FD-number readiness wrappers may survive in
# tracked C/H/assembly sources, including host tests. Distinguish grep errors
# from the expected no-match status.
rc=0
git grep -n -E '\b(nas_epoll_fd_poll|nas_eventfd_poll|nas_timerfd_poll_fd|nas_inotify_poll_fd|nas_pidfd_poll_fd|vpoll_(always|tty|pipe_read|pipe_write|fifo_rw|vt|socket|pty|epoll)|signalfd_poll_fd)\b' -- '*.c' '*.h' '*.S' || rc=$?
[ "$rc" -eq 1 ]
sh scripts/check-m89-poll.sh
sh scripts/check-m90-io-wait.sh
sh scripts/check-host-fixture.sh m141-pidfd-host
sh scripts/check-timerfd.sh
sh scripts/check-m187-inotify.sh
echo '[FILE READINESS PASS] OpenFile/Task readiness, epoll, pidfd, timerfd, inotify'
