#!/bin/sh
# One existing signed APK overlay, no competing M80 installer or guest compiler.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m80-batch
echo "[M80 HOST STAGED] $1 twenty genuine signed Alpine ELF candidates; guest functional results pending"
