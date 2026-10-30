#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m67-control
echo "[M67 STAGE] native original signed pgrep/pkill $1"
