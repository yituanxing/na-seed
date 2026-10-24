#!/bin/sh
# Native three-ISA original signed Alpine Git local-repository acceptance.
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
NAS_ALPINE_PROFILE=git sh rootfs/alpine/build.sh "$port"
work="build/external/m51-git/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m51-git-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m51-git.clean.log"
test -s "$log"
for marker in M51_ORIGINAL_SIGNED_GIT_PASS M51_GIT_INIT_PASS \
 M51_GIT_OBJECT_INDEX_COMMIT_PASS M51_GIT_DIFF_HISTORY_PASS \
 M51_GIT_REFS_CHECKOUT_PASS M51_GIT_SUITE_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M51 FAIL] missing real Git marker $marker ($port)" >&2
    tail -180 "$log" >&2
    exit 1
  fi
done
echo "[M51 PASS] $port original Git init/add/commit/diff/history/branch/checkout"
