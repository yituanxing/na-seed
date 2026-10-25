#!/bin/sh
# Host-only signed Alpine APK + original ELF/CPIO staging.
set -eu
[ "$#" -eq 1 ] || exit 2
sh scripts/stage-alpine-prebuilt-overlay.sh "$1" m59-worktree \
    rootfs/alpine/packages-m59-worktree.txt \
    scripts/m59-original-elfs.txt scripts/m59-rootfs-copies.tsv
echo "[M59 HOST STAGE] $1 original rsync/diffutils/patch/file; no guest compiler"
