#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
 amd64) runner=scripts/probe-x86-busybox-parity.sh
        logs=build/busybox-source-parity-x86 ;;
 riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh
          logs=build/port-riscv64/full/matrix-source ;;
 arm64) runner=scripts/probe-aarch64-busybox-matrix.sh
        logs=build/port-aarch64/full/matrix-source ;;
 *) exit 2 ;;
esac
sh scripts/stage-m69-prebuilt.sh "$port"
work="build/external/m69-inspect/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m69-inspect-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
sh "$runner"
log="$logs/m69-inspect.clean.log"
test -s "$log"
for marker in M69_PROC_THREAD_NAMESPACE_PASS M69_ORIGINAL_PROCPS_THREADS_PASS \
              M69_ORIGINAL_PSTREE_PASS M69_ORIGINAL_TASKSET_AFFINITY_PASS \
              M69_ORIGINAL_PRLIMIT_PASS M69_TRACE_PREP_FULL_PASS; do
 if ! grep -Fxq "$marker" "$log"; then
  echo "[M69 FAIL] $port missing $marker" >&2
  tail -480 "$log" >&2
  exit 1
 fi
done
echo "[M69 PASS] $port original signed pstree / ps -eL / taskset / prlimit"
