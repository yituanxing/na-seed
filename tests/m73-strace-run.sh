#!/bin/sh
# Unmodified signed Alpine strace; all writes and waits occur IN NaSeed.
set -eu
PATH=/usr/bin:/bin; export PATH
LC_ALL=C; export LC_ALL
# A real process tree, not only a single exec+exit.
if ! /usr/bin/strace -f -o /tmp/m73-follow.trace /bin/busybox sh -c   '/bin/busybox sleep 0.01 & wait; /bin/busybox echo follow-ok'   > /tmp/m73-follow.stdout 2> /tmp/m73-follow.stderr; then
  echo '[M73 DIAG] original strace -f child/wait failed'
  /bin/busybox cat /tmp/m73-follow.stderr || :
  /bin/busybox head -c 6000 /tmp/m73-follow.trace || :
  exit 1
fi
/bin/busybox grep -q 'follow-ok' /tmp/m73-follow.stdout
# Need a child pid and syscall record, not merely the -f flag accepted.
if ! /bin/busybox grep -Eq '(clone|fork|vfork)\(' /tmp/m73-follow.trace ||
   ! /bin/busybox grep -q 'wait4(' /tmp/m73-follow.trace ||
   ! /bin/busybox grep -q 'nanosleep(' /tmp/m73-follow.trace; then
  echo '[M73 FAIL] original strace missed fork, wait4 or sleep'
  /bin/busybox head -c 6000 /tmp/m73-follow.trace || :
  exit 1
fi
echo M73_BLOCKED_SYSCALL_PASS
