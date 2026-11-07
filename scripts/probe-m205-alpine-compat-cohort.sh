#!/bin/sh
# Extend the already proven M197 physical Alpine software runner; no new
# guest PID1, kernel ABI, network stack, disk implementation, or QEMU driver.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m205-alpine-compat-cohort.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M205_COMPAT_COHORT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
