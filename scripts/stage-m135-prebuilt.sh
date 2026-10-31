#!/bin/sh
# Six signed software families, 15 new original ELF programs on each ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = scripts/m135-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M135 DUPLICATE ORIGINAL ELF] $elf previously in $old" >&2
            exit 1
        fi
    done
done < scripts/m135-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m135-batch
work="build/external/m135-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in cmake libjpeg-turbo-utils tidyhtml gdbm-tools graphviz netpbm; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M135 HOST STAGE FAIL] $port unsigned/missing $pkg" >&2
        exit 1
    }
done
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$elf") || exit 1
    case "$target" in "$root/bin/busybox"|"$root/usr/bin/busybox")
        echo "[M135 HOST STAGE FAIL] BusyBox alias $elf" >&2
        exit 1 ;;
    esac
    test -x "$target"
    sha256sum "$target"
    count=$((count + 1))
done < scripts/m135-original-elfs.txt > "$work/m135-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m135-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M135 HOST STAGE FAIL] $port only $count paths/$unique distinct signed SHA256" >&2
    exit 1
fi
echo "[M135 HOST STAGE PASS] $port 15 newly signed and distinct ELF files"
