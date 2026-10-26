#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
# Original distro-signed host APKs; no in-guest source build or replacement ELF.
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m72-strace \
  rootfs/alpine/packages-m72-strace.txt \
  scripts/m72-original-elfs.txt scripts/m72-rootfs-copies.tsv
echo "[M72 HOST STAGED] original signed Alpine strace $1"
