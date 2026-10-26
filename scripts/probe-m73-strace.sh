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
sh scripts/stage-m73-prebuilt.sh "$port"
work="build/external/m73-strace/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m73-strace-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
# An independent evidence log is retained on each native ISA failure.
if ! sh "$runner"; then
  log="$logs/m73-strace.clean.log"
  echo "[M73 FAIL] $port original strace could not trace the original process" >&2
  test ! -s "$log" || tail -360 "$log" >&2
  exit 1
fi
log="$logs/m73-strace.clean.log"
test -s "$log"
for marker in M73_BLOCKED_SYSCALL_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M73 FAIL] $port missing $marker" >&2
    tail -360 "$log" >&2
    exit 1
  fi
done
echo "[M73 PASS] $port genuine original Alpine strace multi-task observations"
