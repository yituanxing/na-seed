#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m67-prebuilt.sh "$port"
work="build/external/m67-control/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m67-control-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
sh "$runner"
log="$logs/m67-control.clean.log"
test -s "$log"
for marker in M67_REAL_TMUX_PTYS_PASS M67_STD_DEVICE_NULL_PASS M67_ORIGINAL_PGREP_CMDLINE_PASS M67_ORIGINAL_PGREP_COMM_PASS \
              M67_ORIGINAL_PKILL_STOP_CONT_PASS \
              M67_ORIGINAL_PKILL_TERM_WAIT_PASS \
              M67_ORIGINAL_PKILL_KILL_WAIT_PASS \
              M67_ORIGINAL_PGREP_REAPED_PASS M68_ASH_BACKGROUND_WAIT_PASS \
              M68_ASH_FD0_REOPEN_PASS M68_ASH_BACKGROUND_PIPE_PASS \
              M67_PROCPS_CONTROL_FULL_PASS; do
 if ! grep -Fxq "$marker" "$log"; then
  echo "[M67 FAIL] $port missing $marker" >&2
  tail -480 "$log" >&2
  exit 1
 fi
done
echo "[M67 PASS] $port original signed procps-ng pgrep/pkill signals, /proc and wait4"
