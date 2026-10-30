#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m69-inspect
echo "[M69 HOST STAGED] original pstree ps taskset prlimit $1"
