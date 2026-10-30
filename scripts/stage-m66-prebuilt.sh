#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m66-monitor
echo "[M66 STAGE] native original signed top/htop/tmux $1"
