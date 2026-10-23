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
sh rootfs/alpine/build.sh "$port"
work="build/external/m38-gcc/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/usr/bin/gcc"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m38-gcc-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
# Acceptance-only resources for the original distro compiler, not a default
# OS configuration change.
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=768
# Opt-in generic syscall trace when diagnosing the x86 native dynamic
# toolchain. Never enable this on normal kernel or other ISA CI shards.
if [ "$port" = amd64 ]; then export NAS_SYSCALL_TRACE=1; fi
sh "$runner"
test -s "$logs/m38-gcc.clean.log"
grep -Fxq M38_GCC_PASS "$logs/m38-gcc.clean.log"
echo "[M38 GCC PASS] $port original Alpine GCC + musl builds/links/runs Linux C ELF in NaSeed"
