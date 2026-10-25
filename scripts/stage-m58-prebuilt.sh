#!/bin/sh
# Existing signed Alpine M58 package overlay, now shared by future apps.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m58-runtime \
    rootfs/alpine/packages-m58-runtime.txt \
    scripts/m58-original-elfs.txt scripts/m58-rootfs-copies.tsv
echo "[M58 HOST STAGE] $1 signed original Bash/Tcl/OpenSSL, zero guest compilation"
