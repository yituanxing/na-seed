#!/bin/sh
# Twelve distinct new signed original Alpine utilities; one first and one
# independent second native QEMU per ISA, inherited from the M197 owner.
set -eu
test "$#" -eq 1 || exit 2
export NAS_M235_COMPAT_COHORT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
