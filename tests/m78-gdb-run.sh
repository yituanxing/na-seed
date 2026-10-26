#!/bin/sh
# Genuine original GNU GDB: two distinct CPU PTRACE_SINGLESTEP user traps.
set -eu
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
if ! /usr/bin/gdb -nx -q -batch \
   -ex 'set pagination off' \
   -ex 'set confirm off' \
   -ex 'set startup-with-shell off' \
   -ex 'break m78-inferior.c:33' \
   -ex 'run' \
   -ex 'print local' \
   -ex 'print/x $pc' \
   -ex 'stepi' \
   -ex 'print/x $pc' \
   -ex 'stepi' \
   -ex 'print/x $pc' \
   -ex 'continue' \
   --args /tests/m78-inferior \
   > /tmp/m78-gdb.stdout 2>/tmp/m78-gdb.stderr; then
  echo '[M78 FAIL] original GDB instruction stepping failed'
  /bin/busybox head -c 12000 /tmp/m78-gdb.stdout || :
  /bin/busybox head -c 8000 /tmp/m78-gdb.stderr || :
  exit 1
fi
if ! /bin/busybox grep -q 'Breakpoint 1, m78_compute' /tmp/m78-gdb.stdout ||
   ! /bin/busybox grep -Eq '\$1 = 17' /tmp/m78-gdb.stdout ||
   ! /bin/busybox grep -Eq '\$2 = 0x[0-9a-f]+' /tmp/m78-gdb.stdout ||
   ! /bin/busybox grep -Eq '\$3 = 0x[0-9a-f]+' /tmp/m78-gdb.stdout ||
   ! /bin/busybox grep -Eq '\$4 = 0x[0-9a-f]+' /tmp/m78-gdb.stdout ||
   ! /bin/busybox grep -q 'M78_C_VARIABLE_CHANGED_PASS' /tmp/m78-gdb.stdout ||
   ! /bin/busybox grep -Eq 'exited normally|exited with code 0' /tmp/m78-gdb.stdout; then
  echo '[M78 FAIL] no genuine source PC steps and successful C output'
  /bin/busybox head -c 12000 /tmp/m78-gdb.stdout || :
  /bin/busybox head -c 8000 /tmp/m78-gdb.stderr || :
  exit 1
fi
before=$(/bin/busybox awk '/^\$2 = 0x/{print $3}' /tmp/m78-gdb.stdout)
after_one=$(/bin/busybox awk '/^\$3 = 0x/{print $3}' /tmp/m78-gdb.stdout)
after_two=$(/bin/busybox awk '/^\$4 = 0x/{print $3}' /tmp/m78-gdb.stdout)
if [ -z "$before" ] || [ -z "$after_one" ] || [ -z "$after_two" ] ||
   [ "$before" = "$after_one" ] || [ "$after_one" = "$after_two" ]; then
  echo '[M78 FAIL] a step did not advance the real inferior PC'
  exit 1
fi
echo M78_REAL_STEPI_PASS
