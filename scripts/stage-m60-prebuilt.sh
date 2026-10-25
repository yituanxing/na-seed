#!/bin/sh
# Host-only original Alpine binaries: no guest package installation or build.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m60-terminal \
    rootfs/alpine/packages-m60-terminal.txt \
    scripts/m60-original-elfs.txt scripts/m60-rootfs-copies.tsv
echo "[M60 HOST STAGE] $1 original signed less/tmux, no guest compiler"
