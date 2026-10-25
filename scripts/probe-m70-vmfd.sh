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
sh scripts/stage-m70-prebuilt.sh "$port"
work="build/external/m70-vmfd/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m70-vmfd-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
sh "$runner"
log="$logs/m70-vmfd.clean.log"
test -s "$log"
for marker in M70_PROC_FD_PASS M70_PROC_MAPS_PASS \
              M70_ORIGINAL_PMAP_PASS M70_ORIGINAL_LSOF_PASS \
              M70_VMFD_FULL_PASS; do
 if ! grep -Fxq "$marker" "$log"; then
  echo "[M70 FAIL] $port missing $marker" >&2
  tail -480 "$log" >&2
  exit 1
 fi
done
echo "[M70 PASS] $port original signed pmap/lsof against real native Task VMA and FD objects"
