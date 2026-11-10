#!/bin/sh
# Five executable original-Alpine M213 probes, no new VM/installer/guest PID1.
# The same native M197 public HTTPS and independent reopened ext4 pipeline.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m215-alpine-pilot.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M215_PILOT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
