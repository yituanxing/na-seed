#!/bin/sh
# Signed Alpine Netpbm original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m125-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M125 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m125-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m125-batch
work="build/external/m125-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed"
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$root/bin/busybox"|"$root/usr/bin/busybox")
            echo "[M125 HOST STAGE FAIL] BusyBox alias masquerading as original ELF: $elf" >&2
            exit 1 ;;
    esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m125-original-elfs.txt > "$work/m125-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m125-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M125 HOST STAGE FAIL] $port $count programs, $unique distinct SHA256" >&2
    exit 1
fi
echo "[M125 HOST STAGE PASS] $port 15 globally new native Netpbm original ELFs"
