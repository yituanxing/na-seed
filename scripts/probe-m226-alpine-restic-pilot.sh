#!/bin/sh
# Previously signed/restic M137 ELF, entirely new true remote REST two-boot.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m226-alpine-restic-pilot.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M226_PILOT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
