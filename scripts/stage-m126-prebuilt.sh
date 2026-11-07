#!/bin/sh
# Signed Alpine groff original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m126-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M126 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m126-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m126-batch
work="build/external/m126-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:groff' "$root/lib/apk/db/installed"
# GNU groff preprocessors and output drivers depend on their original font,
# macro, and device definitions; a 15-binary-only rootfs cannot typeset text.
for resource in font tmac; do
    dir="$root/usr/share/groff/1.23.0/$resource"
    if ! test -d "$dir" || ! find "$dir" -type f -print -quit | grep -q .; then
        echo "[M126 HOST STAGE FAIL] missing signed groff resource directory: $dir" >&2
        exit 1
    fi
done
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$root/bin/busybox"|"$root/usr/bin/busybox")
            echo "[M126 HOST STAGE FAIL] BusyBox alias masquerading as original ELF: $elf" >&2
            exit 1 ;;
    esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m126-original-elfs.txt > "$work/m126-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m126-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M126 HOST STAGE FAIL] $port $count programs, $unique distinct SHA256" >&2
    exit 1
fi
echo "[M126 HOST STAGE PASS] $port 15 globally new native groff original ELFs"
