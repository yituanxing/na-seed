#!/bin/sh
# M77: original GNU GDB attaches to an EXISTING live pid (not TRACEME),
# sets a native software breakpoint, mutates actual DWARF local, detaches.
set -eu
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
/tests/m77-inferior > /tmp/m77-inferior.stdout 2>/tmp/m77-inferior.stderr &
child=$!
cleanup() {
  /bin/busybox kill "$child" 2>/dev/null || :
}
trap cleanup 0 1 2 3 15
# Allow the inferior to reach a real sleeping syscall before PTRACE_ATTACH.
/bin/busybox sleep 1
if ! /usr/bin/gdb -nx -q -batch \
   -ex 'set pagination off' \
   -ex 'set confirm off' \
   -ex "file /tests/m77-inferior" \
   -ex "attach $child" \
   -ex 'info inferiors' \
   -ex 'break m77-inferior.c:33' \
   -ex 'continue' \
   -ex 'print local' \
   -ex 'set variable local=94' \
   -ex 'print local' \
   -ex 'detach' \
   > /tmp/m77-gdb.stdout 2>/tmp/m77-gdb.stderr; then
  echo '[M77 FAIL] unmodified GNU GDB could not attach and debug real running pid'
  /bin/busybox head -c 12000 /tmp/m77-gdb.stdout || :
  /bin/busybox head -c 8000 /tmp/m77-gdb.stderr || :
  /bin/busybox head -c 2000 /tmp/m77-inferior.stdout || :
  exit 1
fi
if ! /bin/busybox grep -Eq 'process [0-9]+' /tmp/m77-gdb.stdout ||
   ! /bin/busybox grep -q 'Breakpoint 1, m77_compute' /tmp/m77-gdb.stdout ||
   ! /bin/busybox grep -Eq '\\$1 = 17' /tmp/m77-gdb.stdout ||
   ! /bin/busybox grep -Eq '\\$2 = 94' /tmp/m77-gdb.stdout ||
   ! /bin/busybox grep -q 'Detaching from program' /tmp/m77-gdb.stdout; then
  echo '[M77 FAIL] missing real attach, breakpoint, live DWARF mutation or detach'
  /bin/busybox head -c 12000 /tmp/m77-gdb.stdout || :
  /bin/busybox head -c 8000 /tmp/m77-gdb.stderr || :
  exit 1
fi
if ! wait "$child"; then
  echo '[M77 FAIL] detached original inferior did not exit normally'
  /bin/busybox head -c 2000 /tmp/m77-inferior.stdout || :
  exit 1
fi
if ! /bin/busybox grep -Fxq 'M77_INFERIOR_READY' /tmp/m77-inferior.stdout ||
   ! /bin/busybox grep -Fxq 'M77_ATTACH_MUTATION_PASS' /tmp/m77-inferior.stdout; then
  echo '[M77 FAIL] inferior itself did not observe the debugger mutation'
  /bin/busybox head -c 2000 /tmp/m77-inferior.stdout || :
  exit 1
fi
trap - 0 1 2 3 15
echo M77_GDB_ATTACH_PASS
