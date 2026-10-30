#!/bin/sh
# M85: original signed Alpine Git, real VirtIO ext4 object packs and refs,
# two independent full QEMU boots on the SAME raw disk for three ISAs.
# Reuse M83/M84 generic QEMU+disk execution; never copy that runner here.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m85-git.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
. scripts/native-source-qemu.sh
sh scripts/stage-m85-git.sh "$port"
work="build/external/m85-git/$port"
nas_source_guest_env "$work/rootfs" 1024M
disk="$work/m85-physical-ext4.img"
nas_source_prepare_disk "$disk" 256
nas_source_build_kernel m85

first_markers='M85_ORIGINAL_SIGNED_GIT_PASS M85_EXT4_GIT_OBJECT_INDEX_PASS M85_EXT4_GIT_RENAME_BRANCH_PASS M85_EXT4_GIT_GC_REPACK_FSCK_PASS M85_FIRST_BOOT_PASS'
nas_source_run_case m85-first scripts/m85-first-cases.tsv "$first_markers"
sh scripts/check-m85-physical-git.sh first "$disk" "$work" "$port"
echo "[M85 FIRST BOOT PASS] $port original signed Git packed repository independently verified on physical ext4"

# Do not reformat, snapshot-copy, truncate or replace the disk between boots.
replay_markers='M85_ORIGINAL_SIGNED_GIT_PASS M85_REPLAY_GIT_PERSISTENT_PACK_PASS M85_REPLAY_GIT_BRANCH_CHECKOUT_PASS M85_REPLAY_PASS'
nas_source_run_case m85-replay scripts/m85-replay-cases.tsv "$replay_markers"
sh scripts/check-m85-physical-git.sh replay "$disk" "$work" "$port"
echo "[M85 PASS] $port original signed Alpine Git branch/rename/commit/gc/repack/fsck, persistent real ext4 packed objects and refs, two genuine QEMU boots, independent host pack validation"
