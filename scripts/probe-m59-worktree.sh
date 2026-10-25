#!/bin/sh
# Native whole-system QEMU, not qemu-user or host-native false positives.
set -eu
[ "$#" -eq 1 ] || exit 2
case "$1" in
  amd64) runner=scripts/probe-x86-busybox-parity.sh
         logs=build/busybox-source-parity-x86 ;;
  riscv64) runner=scripts/probe-riscv64-busybox-matrix.sh
           logs=build/port-riscv64/full/matrix-source ;;
  arm64) runner=scripts/probe-aarch64-busybox-matrix.sh
         logs=build/port-aarch64/full/matrix-source ;;
  *) exit 2 ;;
esac
port=$1
sh scripts/stage-m59-prebuilt.sh "$port"
work="build/external/m59-worktree/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m59-worktree-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m59-worktree.clean.log"
test -s "$log"
for stage in \
    M59_ORIGINAL_RSYNC_PASS \
    M59_RSYNC_TREE_PASS \
    M59_RSYNC_INCREMENTAL_PASS \
    M59_RSYNC_DELETE_PASS \
    M59_DIFF_UNIFIED_PASS \
    M59_PATCH_APPLY_PASS \
    M59_ORIGINAL_FILE_PASS \
    M59_FILE_ELF_MAGIC_PASS \
    M59_FILE_TEXT_MAGIC_PASS \
    M59_FULL_PASS; do
    if ! grep -Fxq "$stage" "$log"; then
        echo "[M59 FAIL] $port missing $stage" >&2
        tail -150 "$log" >&2
        exit 1
    fi
done
echo "[M59 PASS] $port rsync/diff/patch/file real original binaries, all assertions"
