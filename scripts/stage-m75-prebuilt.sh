#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
# Original distro-signed host APKs; no in-guest source build or replacement ELF.
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m75-gdb \
  rootfs/alpine/packages-m75-gdb.txt \
  scripts/m75-original-elfs.txt scripts/m75-rootfs-copies.tsv
echo "[M75 HOST STAGED] original signed Alpine GDB $1"
