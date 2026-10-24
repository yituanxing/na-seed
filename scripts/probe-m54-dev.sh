#!/bin/sh
# Native original five-tool guest development lifecycle. One QEMU boot/ISA.
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
NAS_ALPINE_PROFILE=dev sh rootfs/alpine/build.sh "$port"
work="build/external/m54-dev/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m54-dev-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m54-dev.clean.log"
test -s "$log"
for marker in M54_ORIGINAL_FIVE_NATIVE_TOOLS_PASS \
 M54_GIT_INITIAL_SOURCES_PASS M54_VIM_EDIT_GIT_DIFF_PASS \
 M54_GNU_MAKE_GCC_LINK_RUN_PASS \
 M54_PYTHON_EXEC_GUEST_COMPILED_7_PASS M54_PYTHON_HASH_JSON_7_PASS \
 M54_PYTHON_SQLITE_GUEST_BUILD_7_PASS M54_PYTHON_GIT_WORKTREE_7_PASS \
 M54_GIT_COMMIT_BUILT_SOURCE_PASS M54_INCREMENTAL_VIM_MAKE_REBUILD_PASS \
 M54_PYTHON_EXEC_GUEST_COMPILED_9_PASS M54_PYTHON_HASH_JSON_9_PASS \
 M54_PYTHON_SQLITE_GUEST_BUILD_9_PASS M54_PYTHON_GIT_WORKTREE_9_PASS \
 M54_DEV_FULL_LIFECYCLE_PASS; do
    if ! grep -Fxq "$marker" "$log"; then
       echo "[M54 FAIL] $port missing guest stage $marker" >&2
       tail -180 "$log" >&2
       exit 1
    fi
done
echo "[M54 PASS] $port real Vim -> GNU Make -j2/GCC -> executable -> Python -> Git"
