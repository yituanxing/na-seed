#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m69-inspect \
  rootfs/alpine/packages-m69-inspect.txt \
  scripts/m69-original-elfs.txt scripts/m69-rootfs-copies.tsv
echo "[M69 HOST STAGED] original pstree ps taskset prlimit $1"
