#!/bin/sh
# One signed Alpine v3.23 overlay. The M174 devfs/GUI stage may subsequently
# extend this rootfs and repack CPIO without reconstructing the Alpine overlay.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/m174-software-check-manifest.sh
sh scripts/m187-software-check-manifest.sh
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m174-software-batch
sh scripts/m174-software-stage-check.sh "$port"
sh scripts/m187-software-stage-check.sh "$port"
