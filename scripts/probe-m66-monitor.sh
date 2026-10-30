#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m66-prebuilt.sh "$port"
work="build/external/m66-monitor/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m66-monitor-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
sh "$runner"
log="$logs/m66-monitor.clean.log"
test -s "$log"
for marker in M66_PROCFS_METRICS_PASS M66_ORIGINAL_TOP_PASS \
              M66_ORIGINAL_UPTIME_PASS M66_ORIGINAL_HTOP_ELF_PASS \
              M66_ORIGINAL_HTOP_PTY_PASS M66_MONITOR_FULL_PASS; do
 if ! grep -Fxq "$marker" "$log"; then
  echo "[M66 FAIL] $port missing $marker" >&2
  tail -160 "$log" >&2
  exit 1
 fi
done
echo "[M66 PASS] $port original signed procps-ng top/htop on real procfs"
