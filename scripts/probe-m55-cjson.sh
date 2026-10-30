#!/bin/sh
# All native ISA QEMU boots use exact same cJSON full-upstream CTest logic.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
# RV64 QEMU's original EDK2 VirtualRealTimeClockLib defaults to its old
# compile-time epoch when booting a freshly copied UEFI varstore. The real
# A/B test has proven that pre-seeding RtcEpochSeconds fixes UEFI GetTime()
# without changing EDK2, Limine, or NaSeed. M55 deliberately disables the
# temporary kernel cmdline wall-clock override, exercising firmware RTC.
if [ "$port" = riscv64 ]; then
    export NAS_RV64_RTC_VAR_SEED=1
    export NAS_RV64_RTC_TEST_FIRMWARE=1
fi
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
# The native RV64 guest under single-core software QEMU already passes
# 45/45 original GCC/Ninja actions, 19/19 CTests, and the genuine no-op gate.
# Actual dependent relinks after touching cJSON.c exceed the former 2400s
# ceiling. Allow the remaining original upstream rebuild and second CTest to
# finish; do not skip jobs, shorten CTest, or suppress failures.
if [ "$port" = riscv64 ]; then
    export NAS_M55_QEMU_TIMEOUT=${NAS_M55_QEMU_TIMEOUT:-3300}
else
    export NAS_M55_QEMU_TIMEOUT=${NAS_M55_QEMU_TIMEOUT:-2400}
fi
sh "$runner"
log="$logs/m55-cjson.clean.log"
test -s "$log"
for stage in M55_ORIGINAL_CMAKE_NINJA_CJSON_PASS \
 M55_LINUX_EPOLL_PIPE_WAIT_PASS \
 M55_LINUX_EVENTFD_EPOLL_PASS \
 M55_NATIVE_EVENTFD_VECTOR_PASS M55_NATIVE_PIPE_WRITEV_ATOMIC_PASS \
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
