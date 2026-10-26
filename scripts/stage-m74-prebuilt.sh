#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
# Original distro-signed host APKs; no in-guest source build or replacement ELF.
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m74-strace \
  rootfs/alpine/packages-m74-strace.txt \
  scripts/m74-original-elfs.txt scripts/m74-rootfs-copies.tsv
echo "[M74 HOST STAGED] original signed Alpine strace $1"
