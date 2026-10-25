#!/bin/sh
# Same genuine unmodified upstream BusyBox guest Kbuild on x86/RV/ARM.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m56-native-busybox.sh amd64|riscv64|arm64' >&2; exit 2; }
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
NAS_ALPINE_PROFILE=native-busybox sh rootfs/alpine/build.sh "$port"
work=build/external/m56-native-busybox/"$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m56-native-busybox-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
export NAS_M56_QEMU_TIMEOUT=${NAS_M56_QEMU_TIMEOUT:-3600}
sh "$runner"
log="$logs/m56-busybox.clean.log"
test -s "$log"
for stage in M56_GENUINE_NATIVE_ALPINE_TOOLCHAIN_PASS \
 M56_LINUX_LONG_EXECVE_ARG_PASS M56_LINUX_MAKE_ENV_CAPACITY_PASS \
 M56_LINUX_LARGE_ARGV_VECTOR_PASS M56_UPSTREAM_SOURCE_IN_TMPFS_PASS \
 M56_NATIVE_KCONFIG_PINNED_OPTIONS_PASS \
 M56_REAL_UPSTREAM_KBUILD_LINK_PASS \
 M56_NEW_GUEST_BUILT_BUSYBOX_EXEC_PASS \
 M56_REAL_GUEST_BUSYBOX_FULL_PASS; do
    grep -Fxq "$stage" "$log" || {
        echo "[M56 FAIL] $port missing stage $stage" >&2
        tail -130 "$log" >&2
        exit 1
    }
done
echo "[M56 PASS] $port upstream BusyBox configured/compiled/linked/executed WITHIN NaSeed"
