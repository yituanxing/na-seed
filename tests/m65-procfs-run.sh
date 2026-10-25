#!/bin/sh
# Genuine guest procfs + original Alpine procps-ng. Run every stage even on
# failure, to find multiple common kernel gaps per one three-ISA CI batch.
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
failures=0
if /bin/busybox cat /proc/1/status > /tmp/m65-kernel-status &&
   /bin/busybox grep -q 'Pid:' /tmp/m65-kernel-status &&
   /bin/busybox cat /proc/self/stat > /tmp/m65-kernel-stat &&
   /bin/busybox cat /proc/meminfo > /tmp/m65-kernel-memory &&
   /bin/busybox grep -q 'MemTotal:' /tmp/m65-kernel-memory &&
   /bin/busybox cat /proc/uptime > /tmp/m65-kernel-uptime; then
 echo M65_PROCFS_NATIVE_PASS
else
 echo "[M65 PROCFS FAIL] original BusyBox read/redirect/stat" >&2
 failures=$((failures+1))
fi
if /usr/bin/free -k > /tmp/m65-free &&
   /bin/busybox grep -q 'Mem:' /tmp/m65-free; then
 echo M65_PROCPS_FREE_PASS
else
 echo "[M65 FREE FAIL] original procps-ng free" >&2
 failures=$((failures+1))
fi
if /bin/ps -eo pid,ppid,comm,state > /tmp/m65-ps &&
   /bin/busybox grep -q 'PID' /tmp/m65-ps; then
 echo M65_PROCPS_PS_PASS
else
 echo "[M65 PS FAIL] original procps-ng ps" >&2
 failures=$((failures+1))
fi
test "$failures" -eq 0 || exit 1
echo M65_PROCPS_FULL_PASS
