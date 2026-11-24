#!/bin/sh
# 15 genuinely distinct original native GNU gettext ELF binaries, signed APK.
# Only the Linux HOST assembles rootfs; all translation work is guest runtime.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m119-prebuilt.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m119-batch
work="build/external/m119-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
test -s "$root/tests/m119-catalog.po" && test -s "$root/tests/m119-batch-run.sh"
test -s "$root/tests/m119-catalog.mo"

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m119-original-elfs.txt "$work/m119-original-signed-elfs.sha256" 15 unique M119
echo "[M119 HOST STAGE PASS] $port 15 unique signed GNU gettext ELFs, no guest builds"
