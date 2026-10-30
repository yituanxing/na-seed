#!/bin/sh
# M84 genuine original SQLite 3.54.1 + Lua 5.4.8 native GCC/Make inside
# NaSeed, with one physical ext4 disk kept across two independent QEMU boots.
# The M83 shared host runner owns all QEMU/kernel preparation; no new driver,
# syscall shim, package installer, or duplicate application oracles.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m84-source-build.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
. scripts/native-source-qemu.sh
sh scripts/stage-m84-prebuilt.sh "$port"
work="build/external/m84-source/$port"
nas_source_guest_env "$work/rootfs" 2048M
disk="$work/m84-physical-ext4.img"
nas_source_prepare_disk "$disk" 256
nas_source_build_kernel m84

build_markers='M84_SQLITE_SIGNED_NATIVE_TOOLCHAIN_PASS M84_SQLITE_ORIGINAL_SOURCE_ON_EXT4_PASS M84_SQLITE_NATIVE_BUILD_SQL_ORACLE_PASS M84_SQLITE_INCREMENTAL_NOOP_PASS M84_SQLITE_EXT4_PERSISTENCE_PASS M84_SQLITE_BUILD_PASS M84_LUA_SIGNED_GCC_MAKE_PASS M84_LUA_ORIGINAL_SOURCE_PASS M84_LUA_NATIVE_BUILD_PASS M84_LUA_VM_GC_COROUTINE_AND_IO_PASS M84_LUA_MAKE_NOOP_PASS M84_LUA_SELECTIVE_REBUILD_PASS M84_LUA_CROSS_FS_EXEC_PASS M84_LUA_BUILD_PASS M84_BUILD_PASS'
nas_source_run_case m84-build scripts/m84-build-cases.tsv "$build_markers"
sh scripts/check-ext4-fsck-clean.sh "$disk"

# Independent host verification, directly against the raw ext4 device after
# the guest cleanly unmounted it. These are NOT copied from an APK or host
# compiler: the paths exist only after NaSeed's original GCC/Make finished.
debugfs -R 'cat /m84-result.txt' "$disk" > "$work/m84-build-result.txt"
grep -Fxq 'NaSeed-M84-original-SQLite3-Lua-native-GCC-PASS' "$work/m84-build-result.txt"
debugfs -R "dump /m84-sqlite/sqlite3 $work/m84-built-sqlite.elf" \
    "$disk" > "$work/m84-build-debugfs-sqlite.log" 2>&1
debugfs -R "dump /m84-lua-5.4.8/src/lua $work/m84-built-lua.elf" \
    "$disk" > "$work/m84-build-debugfs-lua.log" 2>&1
debugfs -R "dump /m84-sqlite.db $work/m84-built-sqlite.db" \
    "$disk" > "$work/m84-build-debugfs-db.log" 2>&1
for output in "$work/m84-built-sqlite.elf" "$work/m84-built-lua.elf" \
              "$work/m84-built-sqlite.db"; do
    test -s "$output" || { echo "[M84 FAIL] missing physical guest output: $output" >&2; exit 1; }
done
machine=$(nas_source_elf_machine)
for output in "$work/m84-built-sqlite.elf" "$work/m84-built-lua.elf"; do
    readelf -h "$output" | grep -Fq "$machine" || {
        echo "[M84 FAIL] $port ELF produced by guest has wrong ISA: $output" >&2
        exit 1
    }
done
head -c 16 "$work/m84-built-sqlite.db" | grep -aFq 'SQLite format 3' || {
    echo '[M84 FAIL] physical SQLite file lacks authentic database magic' >&2
    exit 1
}
sha256sum "$work/m84-built-sqlite.elf" "$work/m84-built-lua.elf" \
          "$work/m84-built-sqlite.db" > "$work/m84-built-guest-objects.sha256"
echo "[M84 FIRST BOOT PASS] $port original guest-built SQLite/Lua ELF and real SQLite database independently extracted from clean ext4"

# This invokes a whole new QEMU machine from the same unchanged raw disk;
# no cp, truncate, mke2fs or image reset between the two runs.
replay_markers='M84_SQLITE_SIGNED_NATIVE_TOOLCHAIN_PASS M84_SQLITE_REPLAY_DB_INTEGRITY_PASS M84_SQLITE_REPLAY_MAKE_NOOP_PASS M84_SQLITE_REPLAY_PASS M84_LUA_SIGNED_GCC_MAKE_PASS M84_LUA_REPLAY_EXEC_PASS M84_LUA_REPLAY_NOOP_PASS M84_LUA_REPLAY_PASS M84_REPLAY_PASS'
nas_source_run_case m84-replay scripts/m84-replay-cases.tsv "$replay_markers"
sh scripts/check-ext4-fsck-clean.sh "$disk"
debugfs -R 'cat /m84-result.txt' "$disk" > "$work/m84-replay-result.txt"
cmp "$work/m84-build-result.txt" "$work/m84-replay-result.txt"
debugfs -R "dump /m84-sqlite/sqlite3 $work/m84-replay-sqlite.elf" \
    "$disk" > "$work/m84-replay-debugfs-sqlite.log" 2>&1
debugfs -R "dump /m84-lua-5.4.8/src/lua $work/m84-replay-lua.elf" \
    "$disk" > "$work/m84-replay-debugfs-lua.log" 2>&1
debugfs -R "dump /m84-sqlite.db $work/m84-replay-sqlite.db" \
    "$disk" > "$work/m84-replay-debugfs-db.log" 2>&1
cmp "$work/m84-built-sqlite.elf" "$work/m84-replay-sqlite.elf"
cmp "$work/m84-built-lua.elf" "$work/m84-replay-lua.elf"
cmp "$work/m84-built-sqlite.db" "$work/m84-replay-sqlite.db"
echo "[M84 PASS] $port original native SQLite/Lua GCC/Make, real database transactions, three providers, two QEMU boots, immutable ELF/database replay and clean e2fsck"
