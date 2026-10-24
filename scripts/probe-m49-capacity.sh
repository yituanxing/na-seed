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
NAS_ALPINE_PROFILE=capacity sh rootfs/alpine/build.sh "$port"
work="build/external/m49-capacity/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m49-capacity-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
# Acceptance-only resources for the original distro compiler, not a default
# OS configuration change.
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=768
# Opt-in generic syscall trace when diagnosing the x86 native dynamic
# toolchain. Never enable this on normal kernel or other ISA CI shards.
export NAS_SYSCALL_TRACE=0
sh "$runner"
log="$logs/m49-capacity.clean.log"
test -s "$log"
for marker in M49_ORIGINAL_GNU_BINARIES_PASS \
 M49_MAKE_ALPHA_PARALLEL_PASS M49_MAKE_BETA_PARALLEL_PASS \
 M49_MAKE_GAMMA_PARALLEL_PASS M49_MAKE_DELTA_PARALLEL_PASS \
 M49_GNU_MAKE_J4_PASS M49_FOUR_PARALLEL_COMPILERS_PASS \
 M49_XARGS_alpha_PASS M49_XARGS_beta_PASS \
 M49_XARGS_gamma_PASS M49_XARGS_delta_PASS \
 M49_FOUR_PARALLEL_XARGS_PASS \
 M49_24_SIMULTANEOUS_CHILDREN_PASS M49_24_CHILDREN_WAIT4_REAP_PASS \
 M49_24_CHILDREN_KERNEL_PASS M49_INCREMENTAL_PARALLEL_MAKE_PASS \
 M49_NATIVE_CAPACITY_SUITE_PASS; do
  if ! grep -Fxq "$marker" "$log"; then
    echo "[M49 FAIL] missing $marker ($port)" >&2
    tail -240 "$log" >&2
    exit 1
  fi
done
echo "[M49 PASS] $port 48-task kernel, real GNU Make -j4/GCC, xargs -P4, 24 fork+wait4"
