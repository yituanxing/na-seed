#!/bin/sh
# Signed Alpine audio original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m127-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M127 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m127-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m127-batch
work="build/external/m127-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in wavpack vorbis-tools speex-tools flac lame mpg123; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M127 HOST STAGE FAIL] $port signed APK missing $package" >&2
        exit 1
    }
done
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$root/bin/busybox"|"$root/usr/bin/busybox")
            echo "[M127 HOST STAGE FAIL] BusyBox alias masquerading as original ELF: $elf" >&2
            exit 1 ;;
    esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m127-original-elfs.txt > "$work/m127-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m127-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M127 HOST STAGE FAIL] $port $count programs, $unique distinct SHA256" >&2
    exit 1
fi
echo "[M127 HOST STAGE PASS] $port 15 globally new signed audio ELF programs"
