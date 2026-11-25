#!/bin/sh
# One signed Alpine v3.23 overlay. The M174 devfs/GUI stage may subsequently
# extend this rootfs and repack CPIO without reconstructing the Alpine overlay.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/check-software-batch-manifest.sh m188
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m174-software-batch
sh scripts/check-software-batch-stage.sh "$port" m174
sh scripts/check-software-batch-stage.sh "$port" m187
sh scripts/check-software-batch-stage.sh "$port" m188
