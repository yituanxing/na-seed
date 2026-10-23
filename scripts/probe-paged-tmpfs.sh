#!/bin/sh
# Exactly one real-userland M32 capability across the same three native ISAs.
# Reuse existing verified Debian fixtures and real QEMU parity harnesses;
# do not add a second ELF loader, rootfs generator, or guest-only test tool.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
case "$1" in
    amd64) harness=scripts/probe-x86-busybox-parity.sh ;;
    riscv64) harness=scripts/probe-riscv64-busybox-matrix.sh ;;
    arm64) harness=scripts/probe-aarch64-busybox-matrix.sh ;;
    *) echo "unsupported ISA: $1" >&2; exit 2 ;;
esac
export BUSYBOX_PARITY_MANIFEST=scripts/m32-paged-tmpfs-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
# Test always uses the same genuine pinned Debian binary, not a substitute.
unset BUSYBOX_PARITY_BINARY
sh "$harness"
echo "[M32 PAGED TMPFS PASS] $1 original Debian BusyBox copied from CPIO and execve'd from tmpfs"
