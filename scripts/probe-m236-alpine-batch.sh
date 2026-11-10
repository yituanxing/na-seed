#!/bin/sh
# Same M197 signed public-Alpine first/reopen QEMU owner as M235.
set -eu
test "$#" -eq 1 || exit 2
export NAS_M236_COMPAT_COHORT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
