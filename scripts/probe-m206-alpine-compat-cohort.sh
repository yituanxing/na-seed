#!/bin/sh
# M206 is a new real-ELF software cohort, not a new virtual machine or rootfs.
# Use the proven M191/M197 physical Alpine first boot + second independent QEMU.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m206-alpine-compat-cohort.sh amd64|riscv64|arm64' >&2
    exit 2
}
export NAS_M206_COMPAT_COHORT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
