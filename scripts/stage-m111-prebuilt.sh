#!/bin/sh
# Fourth original-software batch. All signed APKs installed on host Linux.
# No package manager or compiler is invoked inside NaSeed.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m111-prebuilt.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m111-batch
work="build/external/m111-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
count=0
while IFS= read -r guest_elf; do
    case "$guest_elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$guest_elf"
    count=$((count + 1))
done < scripts/m111-original-elfs.txt > "$work/m111-original-signed-elfs.sha256"
if [ "$count" -ne 15 ] || ! test -s "$work/m111-original-signed-elfs.sha256"; then
    echo "[M111 HOST STAGE FAIL] $port expected 15 signed native ELFs, got $count" >&2
    exit 1
fi
echo "[M111 HOST STAGE PASS] $port 15 distinct signed original Linux ELFs, no guest build"
