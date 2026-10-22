#!/bin/sh
# Real QEMU IRQ32/PIT acceptance using original pinned BusyBox. The common
# 16-case regression remains unchanged and mandatory for all three ISAs.
set -eu
port=${1:-amd64}
test "$port" = amd64
binary=build/source-busybox/amd64/busybox
test -s "$binary"
test -s "${BUSYBOX_EXTRA_DIR:-}/m28-preempt"
BUSYBOX_PARITY_BINARY="$binary" \
BUSYBOX_PARITY_MANIFEST=scripts/m28-x86-preempt-cases.tsv \
BUSYBOX_PARITY_EXPECTED_COUNT=1 \
  sh scripts/probe-x86-busybox-parity.sh
log=build/busybox-source-parity-x86/timer-preempt.clean.log
test -s "$log"
grep -Fq '[M28] timer-preempt from=' "$log" || {
    echo '[M28 FAIL] no actual hardware timer task switch reported' >&2
    tail -80 "$log" >&2
    exit 1
}
grep -Fxq 'cpu-hog-did-not-starve-ash' "$log"
echo '[M28 PREEMPT PASS] x86 QEMU PIT interrupts stock BusyBox ash CPU spin'
