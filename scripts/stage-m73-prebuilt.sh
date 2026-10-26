#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
# Original distro-signed host APKs; no in-guest source build or replacement ELF.
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m73-strace \
  rootfs/alpine/packages-m73-strace.txt \
  scripts/m73-original-elfs.txt scripts/m73-rootfs-copies.tsv
echo "[M73 HOST STAGED] original signed Alpine strace $1"
