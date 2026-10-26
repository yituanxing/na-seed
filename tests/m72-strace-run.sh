#!/bin/sh
# Original Alpine v3.23 strace running INSIDE the native NaSeed QEMU guest.
# No mocked syscall responses, no synthetic trace or success marker.
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
if ! /usr/bin/strace -V > /tmp/m72-strace-version 2>&1; then
  echo '[M72 FAIL] original ELF cannot run even in version mode'
  /bin/busybox cat /tmp/m72-strace-version || :
  exit 1
fi
echo M72_ORIGINAL_STRACE_LOAD_PASS
# First genuine ptrace reproducer. Must observe both execve and exit_group
# from the ORIGINAL process, not strace -V or synthetic host-generated text.
if ! /usr/bin/strace -o /tmp/m72-trace /bin/busybox true \
       >/tmp/m72-stdout 2>/tmp/m72-stderr; then
  echo '[M72 DIAG] original strace /bin/busybox true failed'
  /bin/busybox cat /tmp/m72-stderr || :
  /bin/busybox head -c 4096 /tmp/m72-trace || :
  exit 1
fi
if ! /bin/busybox grep -q 'execve(' /tmp/m72-trace ||
   ! /bin/busybox grep -q 'exit_group(' /tmp/m72-trace; then
  echo '[M72 FAIL] strace returned success without observing execve+exit_group'
  /bin/busybox head -c 4096 /tmp/m72-trace || :
  exit 1
fi
echo M72_GENUINE_PTRACE_PASS
