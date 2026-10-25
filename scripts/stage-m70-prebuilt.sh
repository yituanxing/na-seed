#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m70-vmfd \
  rootfs/alpine/packages-m70-vmfd.txt \
  scripts/m69-original-elfs.txt scripts/m69-rootfs-copies.tsv
echo "[M70 HOST STAGED] original pmap and lsof $1"
