#!/bin/sh
# One signed host APK overlay per ISA; no in-guest compiler or package manager.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m65-observe \
  rootfs/alpine/packages-m65-observe.txt \
  scripts/m65-original-elfs.txt scripts/m65-rootfs-copies.tsv
echo "[M65 HOST STAGE] $1 original signed procps-ng"
