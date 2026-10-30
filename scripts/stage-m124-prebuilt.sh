#!/bin/sh
# Signed Alpine util-linux original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m124-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M124 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m124-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m124-batch
work="build/external/m124-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:util-linux-misc' "$root/lib/apk/db/installed"
test -s "$root/tests/m124-empty-minix.img"
test -s "$root/tests/m124-empty-swap.img"
test -s "$root/tests/m124-cramfs-source/hello.txt"
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$root/bin/busybox"|"$root/usr/bin/busybox")
            echo "[M124 HOST STAGE FAIL] BusyBox alias masquerading as original ELF: $elf" >&2
            exit 1 ;;
    esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m124-original-elfs.txt > "$work/m124-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m124-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M124 HOST STAGE FAIL] $port $count programs, $unique distinct SHA256" >&2
    exit 1
fi
echo "[M124 HOST STAGE PASS] $port 15 globally new native util-linux original ELFs"
