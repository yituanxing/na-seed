#!/bin/sh
# Fail-closed original GNU GDB C source-level breakpoint, DWARF local read,
# local write and native resume; ELF built with Clang on the HOST only.
set -eu
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
if ! /usr/bin/gdb -nx -q -batch \
   -ex 'set pagination off' \
   -ex 'set confirm off' \
   -ex 'set startup-with-shell off' \
   -ex 'break m76-inferior.c:33' \
   -ex 'run' \
   -ex 'print local' \
   -ex 'set variable local=94' \
   -ex 'print local' \
   -ex 'continue' \
   --args /tests/m76-inferior \
   > /tmp/m76-gdb.stdout 2>/tmp/m76-gdb.stderr; then
  echo '[M76 FAIL] original GNU GDB C source breakpoint/local modification failed'
  /bin/busybox head -c 10000 /tmp/m76-gdb.stdout || :
  /bin/busybox head -c 8000 /tmp/m76-gdb.stderr || :
  exit 1
fi
if ! /bin/busybox grep -q 'Breakpoint 1, m76_compute' /tmp/m76-gdb.stdout ||
   ! /bin/busybox grep -Eq '\$1 = 17' /tmp/m76-gdb.stdout ||
   ! /bin/busybox grep -Eq '\$2 = 94' /tmp/m76-gdb.stdout ||
   ! /bin/busybox grep -q 'M76_C_VARIABLE_CHANGED_PASS' /tmp/m76-gdb.stdout ||
   ! /bin/busybox grep -Eq 'exited normally|exited with code 0' /tmp/m76-gdb.stdout; then
  echo '[M76 FAIL] no genuine C breakpoint/DWARF read-write/proven modified execution'
  /bin/busybox head -c 10000 /tmp/m76-gdb.stdout || :
  /bin/busybox head -c 8000 /tmp/m76-gdb.stderr || :
  exit 1
fi
echo M76_C_SOURCE_DEBUG_PASS
