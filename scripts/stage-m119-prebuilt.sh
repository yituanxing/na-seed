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

count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m119-original-elfs.txt > "$work/m119-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m119-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$count" -eq 15 ] && [ "$unique" -eq 15 ] || {
    echo "[M119 STAGE FAIL] $port $count native ELF files, $unique unique SHA256" >&2
    exit 1
}
echo "[M119 HOST STAGE PASS] $port 15 unique signed GNU gettext ELFs, no guest builds"
