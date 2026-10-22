#!/bin/sh
# Original Debian BusyBox date, not a kernel-only mock of gettimeofday.
# This is the compact, genuine ARM64 gate for M31 when a full source
# BusyBox cross-compiler installation stalls CI before tests even begin.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
  amd64)
    runner=scripts/probe-x86-busybox-parity.sh
    log=build/busybox-parity-x86/m31-date.clean.log ;;
  riscv64)
    runner=scripts/probe-riscv64-busybox-matrix.sh
    log=build/port-riscv64/full/matrix/m31-date.clean.log ;;
  arm64)
    runner=scripts/probe-aarch64-busybox-matrix.sh
    log=build/port-aarch64/full/matrix/m31-date.clean.log ;;
  *) exit 2 ;;
esac
unset BUSYBOX_PARITY_CASE BUSYBOX_PARITY_BINARY NAS_ACCEPTANCE_CPIO
export BUSYBOX_PARITY_MANIFEST=scripts/m31-date-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
sh "$runner"
test -s "$log"
# A successful date process with a fabricated 1970 epoch is NOT a pass.
# Accept any UTC time from 2000 through 2099; no pinned wall clock year.
awk '/^[0-9][0-9]*$/ {
  if ($0 + 0 >= 946684800 && $0 + 0 < 4102444800) ok = 1;
}
END {if (!ok) exit 1}' "$log" || {
  echo "[M31 DATE FAIL] Original Linux date did not observe credible realtime" >&2
  tail -90 "$log" >&2
  exit 1
}
echo "[M31 DATE PASS] $1 genuine Linux date observed CLOCK_REALTIME from boot epoch"
