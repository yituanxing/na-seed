#!/bin/sh
# Native Alpine wget/aria2c/lighttpd, two physical ext4 boots and real HTTP.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-m223-alpine-pilot.sh amd64|riscv64|arm64' >&2; exit 2; }
export NAS_M223_PILOT=1
export NAS_M198_INTEGRATED_DESKTOP=0 NAS_M199_ORIGINAL_OPENRC_DESKTOP=0
exec sh scripts/probe-m197-alpine-software-cohort.sh "$1"
