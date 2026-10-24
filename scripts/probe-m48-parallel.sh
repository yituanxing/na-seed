#!/bin/sh
# Actual distro-built GCC + musl in a three-ISA NaSeed kernel. No fake libc.
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
NAS_ALPINE_PROFILE=parallel sh rootfs/alpine/build.sh "$port"
work="build/external/m48-parallel/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m48-parallel-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
# Acceptance-only resources for the original distro compiler, not a default
# OS configuration change.
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=768
# Opt-in generic syscall trace when diagnosing the x86 native dynamic
# toolchain. Never enable this on normal kernel or other ISA CI shards.
export NAS_SYSCALL_TRACE=0
sh "$runner"
log="$logs/m48-parallel.clean.log"
test -s "$log"
for marker in M48_GNU_ORIGINAL_BINARIES_PASS \
 M48_MAKE_PARALLEL_A_PASS M48_MAKE_PARALLEL_B_PASS \
 M38_GCC_PASS M38_READV_PASS M48_MAKE_GCC_BUILD_PASS \
 M48_PARALLEL_MAKE_PASS M48_XARGS_WORKER_alpha \
 M48_XARGS_WORKER_beta M48_PARALLEL_XARGS_PASS \
 M48_INCREMENTAL_REBUILD_PASS M48_NATIVE_PARALLEL_SUITE_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M48 FAIL] missing marker $marker ($port)" >&2
    tail -180 "$log" >&2
    exit 1
  fi
done
echo "[M48 PASS] $port original GNU Make -j2, original GCC/musl, xargs -P2"
