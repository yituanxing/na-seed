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
NAS_ALPINE_PROFILE=robust sh rootfs/alpine/build.sh "$port"
work="build/external/m45-robust/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m45-robust-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=0
sh "$runner"
log="$logs/m45-robust.clean.log"
test -s "$log"
for marker in M45_REGISTRATION_PASS M45_ROBUST_OWNER_DIED_PASS \
              M45_PENDING_AND_MULTI_LOCK_PASS M43_THREADS_PASS \
              M45_ROBUST_AND_ZSTD_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M45 FAIL] missing $marker ($port)" >&2
    tail -160 "$log" >&2
    exit 1
  fi
done
echo "[M45 PASS] $port Linux robust futex recovery + genuine zstd pthreads"
