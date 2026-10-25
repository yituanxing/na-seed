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
sh scripts/stage-m65-prebuilt.sh "$port"
work="build/external/m65-observe/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m65-procfs-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
sh "$runner"
log="$logs/m65-procfs.clean.log"
test -s "$log"
for marker in M65_PROCFS_NATIVE_PASS M65_PROCPS_FREE_PASS \
              M65_PROCPS_PS_PASS M65_PROCPS_FULL_PASS; do
 if ! grep -Fxq "$marker" "$log"; then
  echo "[M65 FAIL] $port missing $marker" >&2
  tail -160 "$log" >&2
  exit 1
 fi
done
echo "[M65 PASS] $port original signed procps-ng ps/free, procfs inode/FD/Task"
