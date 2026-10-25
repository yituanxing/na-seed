#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m67-control \
  rootfs/alpine/packages-m67-control.txt \
  scripts/m67-original-elfs.txt scripts/m67-rootfs-copies.tsv
echo "[M67 STAGE] native original signed pgrep/pkill $1"
