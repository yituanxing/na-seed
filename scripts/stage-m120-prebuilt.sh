#!/bin/sh
# Alpine v3.23 signed LibTIFF programs, one original ELF per tested application.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m120-batch
work="build/external/m120-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for fixture in m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw; do
    test -s "$root/tests/$fixture" || exit 1
done

count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m120-original-elfs.txt > "$work/m120-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m120-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M120 HOST STAGE FAIL] $port $count programs, $unique independent ELF hashes" >&2
    exit 1
fi
echo "[M120 HOST STAGE PASS] $port 15 distinct signed original LibTIFF ELFs, host pixel fixtures"
