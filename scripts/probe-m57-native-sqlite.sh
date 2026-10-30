#!/bin/sh
# One exact in-guest upstream SQLite 3.54.1 workload for x86/RV/ARM.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m57-native-sqlite.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=sqlite sh rootfs/alpine/build.sh "$port"
work=build/external/m57-native-sqlite/"$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m57-native-sqlite-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
export NAS_M57_QEMU_TIMEOUT=${NAS_M57_QEMU_TIMEOUT:-3600}
sh "$runner"
log="$logs/m57-sqlite.clean.log"
test -s "$log"
for stage in M57_GENUINE_UPSTREAM_SOURCE_PASS \
 M57_LINUX_LARGE_TMPFS_INODE_PASS \
 M57_NATIVE_SQLITE_CONFIGURE_PASS \
 M57_NATIVE_SQLITE_COMPILE_LINK_PASS \
 M57_NEW_SQLITE_CLI_EXEC_PASS \
 M57_FILE_DB_CREATE_COMMIT_PASS \
 M57_FILE_DB_REOPEN_QUERY_PASS \
 M57_TRANSACTION_ROLLBACK_PASS \
 M57_SQLITE_INDEX_JOIN_PASS \
 M57_SQLITE_INTEGRITY_PASS \
 M57_NATIVE_SQLITE_FULL_PASS; do
    grep -Fxq "$stage" "$log" || {
        echo "[M57 FAIL] $port missing stage $stage" >&2
        tail -160 "$log" >&2
        exit 1
    }
done
echo "[M57 PASS] $port genuine upstream SQLite 3.54.1 configured/built/executed in native NaSeed guest"
