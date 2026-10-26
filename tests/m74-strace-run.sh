#!/bin/sh
# Original Alpine strace attaches to a real unrelated already scheduled
# process, under genuine native NaSeed on all three ISAs.
set -eu
PATH=/usr/bin:/bin; LC_ALL=C
export PATH LC_ALL
# Background child has a real PID and separate Task. Shell does NOT run
# strace as that child's parent: strace must attach across the process tree.
/bin/busybox sh -c '/bin/busybox sleep 1; /bin/busybox echo m74-target-exited' \
  >/tmp/m74-target.stdout 2>/tmp/m74-target.stderr &
target=$!
echo "[M74] target PID $target"
if ! /usr/bin/strace -p "$target" -o /tmp/m74-attach.trace \
     >/tmp/m74-strace.stdout 2>/tmp/m74-strace.stderr; then
  echo '[M74 FAIL] original strace -p target'
  /bin/busybox cat /tmp/m74-strace.stderr || :
  /bin/busybox head -c 5000 /tmp/m74-attach.trace || :
  exit 1
fi
wait "$target"
if ! /bin/busybox grep -q 'm74-target-exited' /tmp/m74-target.stdout ||
   ! /bin/busybox grep -Eq '(wait4|clone|execve|exit_group)\(' /tmp/m74-attach.trace; then
  echo '[M74 FAIL] no true target activity observed by attached strace'
  /bin/busybox cat /tmp/m74-strace.stderr || :
  /bin/busybox head -c 5000 /tmp/m74-attach.trace || :
  exit 1
fi
echo M74_ATTACH_REAL_TRACE_PASS
