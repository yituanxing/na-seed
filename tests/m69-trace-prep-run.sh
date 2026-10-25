#!/bin/sh
# True native kernel + original signed Alpine process-inspection utilities.
# Do not replace missing syscalls with prebuilt fixtures or success markers.
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
fails=0
pid=$$
echo "[M69] actual shell PID=$pid"
if /bin/busybox ls "/proc/$pid/task" > /tmp/m69-tasks &&
   /bin/busybox grep -Fxq "$pid" /tmp/m69-tasks &&
   /bin/busybox cat "/proc/$pid/task/$pid/status" > /tmp/m69-thread-status &&
   /bin/busybox grep -Eq "^Tgid:[[:space:]]+$pid" /tmp/m69-thread-status &&
   /bin/busybox grep -Eq "^Pid:[[:space:]]+$pid" /tmp/m69-thread-status &&
   /bin/busybox cat "/proc/self/task/$pid/stat" > /tmp/m69-thread-stat &&
   /bin/busybox grep -Eq "^$pid \(" /tmp/m69-thread-stat; then
 echo M69_PROC_THREAD_NAMESPACE_PASS
else
 echo "[M69 FAIL] real Task-backed /proc/TGID/task/TID" >&2
 fails=$((fails+1))
fi
if /usr/bin/ps -eL -o pid,lwp,comm > /tmp/m69-ps-threads 2>&1 &&
   /bin/busybox grep -Eq '^[[:space:]]*1[[:space:]]+1[[:space:]]+' /tmp/m69-ps-threads &&
   /bin/busybox grep -Eq "^[[:space:]]*$pid[[:space:]]+$pid[[:space:]]+" /tmp/m69-ps-threads; then
 echo M69_ORIGINAL_PROCPS_THREADS_PASS
else
 echo '[M69 FAIL] original procps-ng ps -eL' >&2
 /bin/busybox head -c 1100 /tmp/m69-ps-threads 2>/dev/null || :
 fails=$((fails+1))
fi
if /usr/bin/pstree -p 1 > /tmp/m69-pstree 2>&1 &&
   /bin/busybox grep -Eq '\(1\)' /tmp/m69-pstree &&
   /bin/busybox grep -Eq "\($pid\)" /tmp/m69-pstree; then
 echo M69_ORIGINAL_PSTREE_PASS
else
 echo '[M69 FAIL] original psmisc pstree -p 1' >&2
 /bin/busybox head -c 1200 /tmp/m69-pstree 2>/dev/null || :
 fails=$((fails+1))
fi
if /usr/bin/taskset -p "$pid" > /tmp/m69-taskset 2>&1 &&
   /bin/busybox grep -qi 'affinity mask' /tmp/m69-taskset; then
 echo M69_ORIGINAL_TASKSET_AFFINITY_PASS
else
 echo '[M69 FAIL] original util-linux taskset -p' >&2
 /bin/busybox head -c 1000 /tmp/m69-taskset 2>/dev/null || :
 fails=$((fails+1))
fi
if /usr/bin/prlimit -p "$pid" --nofile > /tmp/m69-prlimit 2>&1 &&
   /bin/busybox grep -q 'NOFILE' /tmp/m69-prlimit; then
 echo M69_ORIGINAL_PRLIMIT_PASS
else
 echo '[M69 FAIL] original util-linux prlimit --nofile' >&2
 /bin/busybox head -c 1000 /tmp/m69-prlimit 2>/dev/null || :
 fails=$((fails+1))
fi
echo "[M69 SUMMARY] independent failed=$fails"
for file in /tmp/m69-tasks /tmp/m69-thread-status /tmp/m69-ps-threads /tmp/m69-pstree /tmp/m69-taskset /tmp/m69-prlimit; do
 if test -s "$file"; then
  echo "[M69 DIAG] $file"
  /bin/busybox head -c 1300 "$file" || :
  echo
 fi
done
test "$fails" -eq 0 || exit 1
echo M69_TRACE_PREP_FULL_PASS
