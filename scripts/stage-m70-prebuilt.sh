#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m70-vmfd
echo "[M70 HOST STAGED] original pmap and lsof $1"
