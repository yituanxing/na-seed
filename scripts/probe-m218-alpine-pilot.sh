#!/bin/sh
# Preserve M197's original Alpine, live public HTTPS and two independent
# QEMU/ext4 oracles. M218 selects only the original iproute2 ifstat ELF.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m218-alpine-pilot.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M218_PILOT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
