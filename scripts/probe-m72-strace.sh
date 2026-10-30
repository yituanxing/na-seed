#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m72-prebuilt.sh "$port"
work="build/external/m72-strace/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m72-strace-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
# An independent evidence log is retained on each native ISA failure.
if ! sh "$runner"; then
  log="$logs/m72-strace.clean.log"
  echo "[M72 FAIL] $port original strace could not trace the original process" >&2
  test ! -s "$log" || tail -360 "$log" >&2
  exit 1
fi
log="$logs/m72-strace.clean.log"
test -s "$log"
for marker in M72_ORIGINAL_STRACE_LOAD_PASS M72_GENUINE_PTRACE_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M72 FAIL] $port missing $marker" >&2
    tail -360 "$log" >&2
    exit 1
  fi
done
echo "[M72 PASS] $port genuine original Alpine strace syscall observations"
