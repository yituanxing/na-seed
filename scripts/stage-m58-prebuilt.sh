#!/bin/sh
# Existing signed Alpine M58 package overlay, now shared by future apps.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m58-runtime
echo "[M58 HOST STAGE] $1 signed original Bash/Tcl/OpenSSL, zero guest compilation"
