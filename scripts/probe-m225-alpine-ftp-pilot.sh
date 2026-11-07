#!/bin/sh
# Exactly two original signed Alpine FTP clients, independent two-boot wire+disk.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m225-alpine-ftp-pilot.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M225_PILOT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
