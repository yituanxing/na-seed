#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
  amd64) runner=scripts/probe-x86-busybox-parity.sh
         logs=build/busybox-source-parity-x86 ;;
  riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh
         logs=build/port-riscv64/full/matrix-source ;;
  arm64) runner=scripts/probe-aarch64-busybox-matrix.sh
         logs=build/port-aarch64/full/matrix-source ;;
  *) exit 2 ;;
esac
port=$1
NAS_ALPINE_PROFILE=group sh rootfs/alpine/build.sh "$port"
work="build/external/m44-group/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m44-group-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=0
sh "$runner"
log="$logs/m44-group.clean.log"
test -s "$log"
for marker in M44_WORKER_EXIT_GROUP_PASS M44_WORKER_SURVIVED_LEADER \
              M44_LEADER_DEFER_PASS M44_THREAD_GROUP_PASS \
              M43_THREADS_PASS M44_REAL_ZSTD_AND_GROUP_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M44 FAIL] missing $marker ($port)" >&2
    tail -140 "$log" >&2
    exit 1
  fi
done
grep -Fq '[THREAD GROUP] leader exit deferred' "$log"
grep -Fq '[THREAD GROUP] last worker finalized' "$log"
grep -Fq '[THREAD GROUP] exit_group tgid=' "$log"
echo "[M44 PASS] $port Linux group lifetime + original zstd pthreads"
