#!/bin/sh
# Signed original Alpine binaries, run inside a genuine native NaSeed guest.
PATH=/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
fails=0
pid=$$
echo "[M70] shell PID=$pid"
if /bin/busybox ls "/proc/$pid/fd" >/tmp/m70-fd-list &&
   /bin/busybox ls "/proc/$pid/fdinfo" >/tmp/m70-fdinfo-list &&
   /bin/busybox readlink "/proc/$pid/fd/1" >/tmp/m70-stdout &&
   /bin/busybox cat "/proc/$pid/fdinfo/1" >/tmp/m70-fdinfo &&
   /bin/busybox grep -Eq '^pos:' /tmp/m70-fdinfo;then
 echo M70_PROC_FD_PASS
else
 echo '[M70 FAIL] Task-backed fd/fdinfo readlink and position' >&2
 fails=$((fails+1))
fi
if /bin/busybox cat "/proc/$pid/maps" > /tmp/m70-maps &&
   /bin/busybox grep -Eq '^[0-9a-f]+-[0-9a-f]+[[:space:]]+[r-][w-][x-]p' /tmp/m70-maps;then
 echo M70_PROC_MAPS_PASS
else
 echo '[M70 FAIL] VMA /proc/PID/maps' >&2
 fails=$((fails+1))
fi
if /usr/bin/pmap "$pid" > /tmp/m70-pmap 2>&1 &&
   /bin/busybox grep -Eq 'total[[:space:]]+[0-9]|Kbytes|total kB' /tmp/m70-pmap;then
 echo M70_ORIGINAL_PMAP_PASS
else
 echo '[M70 FAIL] original pmap live PID memory report' >&2
 fails=$((fails+1))
fi
if /usr/bin/lsof -nP -p "$pid" > /tmp/m70-lsof 2>&1 &&
   /bin/busybox grep -Eq '[[:space:]]+FD[[:space:]]+' /tmp/m70-lsof &&
   /bin/busybox grep -Eq "[[:space:]]+$pid[[:space:]]+" /tmp/m70-lsof;then
 echo M70_ORIGINAL_LSOF_PASS
else
 echo '[M70 FAIL] original lsof live PID file descriptions' >&2
 fails=$((fails+1))
fi
echo "[M70 SUMMARY] failed=$fails"
for file in /tmp/m70-fd-list /tmp/m70-stdout /tmp/m70-fdinfo /tmp/m70-maps /tmp/m70-pmap /tmp/m70-lsof; do
 if test -s "$file";then
  echo "[M70 DIAG] $file"
  /bin/busybox head -c 1600 "$file" || :
  echo
 fi
done
test "$fails" -eq 0 || exit 1
echo M70_VMFD_FULL_PASS
