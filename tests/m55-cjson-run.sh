#!/bin/sh
# Real upstream cJSON 1.7.19 (pinned Git commit) built with original
# Alpine CMake + Ninja + GCC inside *one* running native NaSeed guest.
set -eu
phase=start
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[FAIL] M55 stage=$phase rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
src=/tmp/m55-upstream-cjson
out=/tmp/m55-ninja
phase=verify-upstream
/bin/busybox cp -R /tests/m55-cjson "$src"
/usr/bin/cmake --version | /bin/busybox grep -Fq 'cmake version'
/usr/bin/ninja --version | /bin/busybox grep -Eq '^1[.][0-9]+'
/usr/bin/gcc --version | /bin/busybox grep -Fq gcc
test -s "$src/CMakeLists.txt"
test -s "$src/tests/unity/src/unity.c"
echo M55_ORIGINAL_CMAKE_NINJA_CJSON_PASS

phase=generic-event-loop-probe
# GCC itself creates temporary assembly/output files in its current directory.
# The boot shell starts at the read-only CPIO '/', so compile FROM writable
# tmpfs; do not patch GCC or invent writable archive semantics.
cd /tmp
# Real, program-agnostic Linux epoll, pipe and deadline semantics used by
# CMake/libuv. Build with the actual target GCC *in the NaSeed guest*.
# Avoid another full CMake timeout without a failing syscall trace.
/usr/bin/gcc -std=c11 -O2 -Wall -Wextra -Werror -static \
    /tests/m55-epoll.c -o /tmp/m55-epoll
/tmp/m55-epoll
echo M55_LINUX_EPOLL_PIPE_WAIT_PASS

phase=eventfd-real-linux-guest
/usr/bin/gcc -std=c11 -O2 -Wall -Wextra -Werror -static \
    /tests/m55-eventfd.c -o /tmp/m55-eventfd
/tmp/m55-eventfd
echo M55_LINUX_EVENTFD_EPOLL_PASS

phase=shared-main-stack-capacity
/usr/bin/gcc -std=c11 -O2 -Wall -Wextra -Werror -static \
    /tests/m55-stack.c -o /tmp/m55-stack
/tmp/m55-stack
echo M55_NATIVE_SHARED_STACK_CAPACITY_PASS

phase=real-cmake-configure
# CMake 4 removes older policy defaults. Supply the new policy floor via
# a normal command-line option; NEVER patch upstream cJSON CMakeLists.
# A real native configure uses guest compiler detection/try_compile,
# filesystem metadata and process lifecycle. No fake CMake cache.
/bin/busybox mkdir -p "$out"
/usr/bin/cmake -S "$src" -B "$out" -G Ninja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
    -DCMAKE_EXE_LINKER_FLAGS=-static \
    -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_CJSON_TEST=ON \
    -DENABLE_CJSON_UTILS=OFF \
    -DENABLE_CUSTOM_COMPILER_FLAGS=OFF \
    -DENABLE_LOCALES=OFF \
    -DENABLE_TARGET_EXPORT=OFF
test -s "$out/build.ninja"
test -s "$out/CMakeCache.txt"
echo M55_CMAKE_REAL_CONFIGURE_PASS

phase=real-parallel-ninja-gcc
/usr/bin/cmake --build "$out" --parallel 2
test -s "$out/libcjson.a"
test -x "$out/cJSON_test"
echo M55_NINJA_REAL_GCC_COMPILE_LINK_PASS

phase=full-upstream-ctest
/usr/bin/ctest --test-dir "$out" --output-on-failure --parallel 2
echo M55_UPSTREAM_CJSON_CTEST_PASS

phase=incremental-noop
# Preserve unmodified Ninja's real dirty-edge explanations. This is a
# semantic zero-action assertion, not an exception for a specific ISA.
# The original RV64 guest built all 45 targets and passed 19/19 CTests,
# yet the post-build -n check scheduled work. Record WHY before exiting.
/usr/bin/ninja -C "$out" -n -d explain > /tmp/m55-noop.txt 2>&1
echo M55_NINJA_NOOP_OUTPUT_BEGIN
/bin/busybox cat /tmp/m55-noop.txt
echo M55_NINJA_NOOP_OUTPUT_END
if ! /bin/busybox grep -Fq 'no work to do' /tmp/m55-noop.txt; then
    echo "[M55 DIAG] native Ninja found dirty edges after first complete build"
    /bin/busybox stat -c '%n %y %s' \
        "$src/CMakeLists.txt" "$src/cJSON.c" \
        "$out/build.ninja" "$out/CMakeFiles/rules.ninja" \
        "$out/libcjson.a" "$out/.ninja_log" || :
    exit 1
fi
echo M55_NINJA_INCREMENTAL_NOOP_PASS

phase=real-source-rebuild
# Touching guest's private copy of upstream C triggers dependency rebuild,
# without changing *any* upstream source bytes.
/bin/busybox sleep 1
/bin/busybox touch "$src/cJSON.c"
/usr/bin/ninja -C "$out" -d explain -j2 > /tmp/m55-rebuild.txt 2>&1
/bin/busybox grep -Fq 'cJSON.c' /tmp/m55-rebuild.txt
test -s "$out/libcjson.a"
echo M55_NINJA_TRUE_SOURCE_REBUILD_PASS

phase=post-rebuild-upstream-tests
/usr/bin/ctest --test-dir "$out" --output-on-failure --parallel 2
echo M55_UPSTREAM_CJSON_RETEST_PASS
echo M55_CMAKE_NINJA_FULL_SUITE_PASS
