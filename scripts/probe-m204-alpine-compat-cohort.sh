#!/bin/sh
# Discovery extension of the existing M197 original Alpine / ext4 / QEMU path.
# A package failure is recorded as a failure, not silently treated as a pass.
set -eu
if [ "$#" -ne 1 ]; then
    echo 'usage: sh scripts/probe-m204-alpine-compat-cohort.sh amd64|riscv64|arm64' >&2
    exit 2
fi
export NAS_M204_COMPAT_COHORT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
