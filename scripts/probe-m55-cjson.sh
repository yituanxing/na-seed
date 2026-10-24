#!/bin/sh
# All native ISA QEMU boots use exact same cJSON full-upstream CTest logic.
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
NAS_ALPINE_PROFILE=cmake sh rootfs/alpine/build.sh "$port"
work=build/external/m55-cjson/"$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m55-cjson-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
# Full original CTest-enabled CMake/Ninja work is CPU-heavy under software
# QEMU. Keep generic syscall tracing opt-in; failures retain serial evidence.
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
# The three probe drivers use this bounded budget only for M55. The boot
# monitor terminates early on *verified* final PASS, never on partial markers.
export NAS_M55_QEMU_TIMEOUT=${NAS_M55_QEMU_TIMEOUT:-2400}
sh "$runner"
log="$logs/m55-cjson.clean.log"
test -s "$log"
for stage in M55_ORIGINAL_CMAKE_NINJA_CJSON_PASS \
 M55_LINUX_EPOLL_PIPE_WAIT_PASS \
 M55_LINUX_EVENTFD_EPOLL_PASS \
 M55_NATIVE_SHARED_STACK_CAPACITY_PASS \
 M55_CMAKE_REAL_CONFIGURE_PASS M55_NINJA_REAL_GCC_COMPILE_LINK_PASS \
 M55_UPSTREAM_CJSON_CTEST_PASS M55_NINJA_INCREMENTAL_NOOP_PASS \
 M55_NINJA_TRUE_SOURCE_REBUILD_PASS M55_UPSTREAM_CJSON_RETEST_PASS \
 M55_CMAKE_NINJA_FULL_SUITE_PASS; do
    grep -Fxq "$stage" "$log" || {
        echo "[M55 FAIL] $port stage missing $stage" >&2
        tail -180 "$log" >&2
        exit 1
    }
done
echo "[M55 PASS] $port genuine CMake/Ninja builds upstream cJSON; complete CTest twice"
