#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=python sh rootfs/alpine/build.sh "$port"
work="build/external/m52-python/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m52-python-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_ACCEPTANCE_DISK_MB=512
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-1}
sh "$runner"
log="$logs/m52-python.clean.log"
test -s "$log"
for mark in M52_ORIGINAL_SIGNED_PYTHON_PASS M52_PYTHON_BYTECODE_PASS \
 M52_PYTHON_STDLIB_JSON_HASH_ZLIB_PASS M52_PYTHON_TMPFS_PATHLIB_PASS \
 M52_PYTHON_SQLITE_FILE_PASS M52_PYTHON_PTHREAD_PASS \
 M52_PYTHON_SUBPROCESS_PASS M52_PYTHON_SUITE_PASS; do
 if ! grep -Fxq "$mark" "$log"; then
  echo "[M52 FAIL] $port no marker $mark" >&2
  tail -160 "$log" >&2
  exit 1
 fi
done
echo "[M52 PASS] $port CPython, stdlib, SQLite, pthread and subprocess"
