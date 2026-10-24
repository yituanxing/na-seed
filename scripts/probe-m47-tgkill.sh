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
NAS_ALPINE_PROFILE=tgkill sh rootfs/alpine/build.sh "$port"
work="build/external/m47-tgkill/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m47-tgkill-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=0
sh "$runner"
log="$logs/m47-tgkill.clean.log"
test -s "$log"
for marker in M47_WORKER_DIRECTED_PENDING_PASS M47_TGID_TID_VALIDATION_PASS \
  M47_TARGETED_MASKED_WAIT_PASS M47_GROUP_FATAL_WAIT4_PASS \
  M47_THREAD_SIGNALS_PASS M43_THREADS_PASS M47_SIGNALS_AND_ZSTD_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M47 FAIL] missing $marker ($port)" >&2
    tail -160 "$log" >&2
    exit 1
  fi
done
grep -Fq '[THREAD GROUP] exit_group tgid=' "$log"
echo "[M47 PASS] $port directed tgkill/tkill, fatal group, original signed zstd"
