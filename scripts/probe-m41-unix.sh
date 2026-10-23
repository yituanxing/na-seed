#!/bin/sh
# Genuine Linux distro ELF runtime gate, common on all three target ISAs.
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
NAS_ALPINE_PROFILE=unix sh rootfs/alpine/build.sh "$port"
work="build/external/m41-unix/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m41-unix-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=512M
export NAS_ACCEPTANCE_DISK_MB=256
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m41-unix.clean.log"
test -s "$log"
for marker in M41_GNU_ELF_PASS M41_COPY_PASS M41_FIND_EXEC_PASS \
              M41_PIPELINE_PASS M41_RENAME_SHA256_PASS M41_DIFF_EXIT_PASS \
              M41_UNIX_PASS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M41 FAIL] missing $marker ($port)" >&2
        tail -140 "$log" >&2
        exit 1
    }
done
echo "[M41 PASS] $port native GNU Unix pipeline and VFS acceptance"
