#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=exec sh rootfs/alpine/build.sh "$port"
work="build/external/m46-exec/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m46-exec-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=0
sh "$runner"
log="$logs/m46-exec.clean.log"
test -s "$log"
for marker in M46_EXEC_TARGET_PASS M46_LEADER_EXEC_WAIT4_PASS \
  M46_WORKER_EXEC_TGID_WAIT4_PASS M46_MULTITHREAD_EXEC_PASS \
  M43_THREADS_PASS M46_EXEC_AND_ZSTD_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M46 FAIL] missing $marker ($port)" >&2
    tail -180 "$log" >&2
    exit 1
  fi
done
grep -Fq '[THREAD EXEC] worker adopted original process tgid=' "$log"
grep -Fq '[THREAD EXEC] leader reaped siblings tgid=' "$log"
echo "[M46 PASS] $port Linux execve from leader or nonleader + original zstd"
