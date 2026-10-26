#!/bin/sh
# Original signed Alpine GDB controls a real separate inferior through Linux
# ptrace on native NaSeed; fail closed if only the GDB ELF starts.
set -eu
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
if ! /usr/bin/gdb --version >/tmp/m75-gdb-version 2>&1; then
  echo '[M75 FAIL] original GDB ELF could not run'
  /bin/busybox head -c 4000 /tmp/m75-gdb-version || :
  exit 1
fi
echo M75_ORIGINAL_GDB_LOAD_PASS
if ! /usr/bin/gdb -nx -q -batch \
   -ex 'set pagination off' \
   -ex 'set confirm off' \
   -ex 'set startup-with-shell off' \
   -ex 'starti' \
   -ex 'info registers' \
   -ex 'continue' \
   --args /bin/busybox true \
   > /tmp/m75-gdb.stdout 2>/tmp/m75-gdb.stderr; then
  echo '[M75 FAIL] original GDB inferior run/starti/reg/continue failed'
  /bin/busybox head -c 6000 /tmp/m75-gdb.stdout || :
  /bin/busybox head -c 6000 /tmp/m75-gdb.stderr || :
  exit 1
fi
# 'starti' must have selected a real stopped inferior: this is the only
# time original GDB can read its native nonzero PC/RIP register. The status
# phrasing differs by ISA; do not key success to its English wording.
if ! /bin/busybox grep -Eq '^(pc|rip)[[:space:]]+0x[0-9a-fA-F]*[1-9a-fA-F][0-9a-fA-F]*' /tmp/m75-gdb.stdout ||
   ! /bin/busybox grep -Eq 'exited normally|exited with code 0' /tmp/m75-gdb.stdout; then
  echo '[M75 FAIL] GDB returned 0 without a genuine inferior stop and registers'
  /bin/busybox head -c 6000 /tmp/m75-gdb.stdout || :
  /bin/busybox head -c 6000 /tmp/m75-gdb.stderr || :
  exit 1
fi
echo M75_REAL_GDB_INFERIOR_PASS
