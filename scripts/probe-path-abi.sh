#!/bin/sh
# Dedicated syscall ABI probe accompanies, never replaces, real BusyBox.
# Validate exact Linux errno, read-only CPIO and CPIO symlink pathwalk.
set -eu
log=build/path-abi-serial.log
clean=build/path-abi-clean.log
rc=0
timeout 20s sh scripts/run.sh build/naseed-path-abi.iso >"$log" 2>&1 || rc=$?
tr -d '\r' <"$log" >"$clean"
if [ "$rc" -ne 1 ] ||
   ! grep -Fxq '[USER] Linux openat CPIO flags and pathwalk ABI PASS' "$clean" ||
   ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
   ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
   grep -Fq '[FAULT]' "$clean" ||
   grep -Fq '[DEADLOCK]' "$clean"; then
    echo "[FAIL] direct openat CPIO flags and symlink-dotdot rc=$rc" >&2
    tail -120 "$clean" >&2
    exit 1
fi
echo '[ACCEPTED] exact Linux openat O_CREAT/O_EXCL/O_TRUNC/read and ENOTDIR ABI'
