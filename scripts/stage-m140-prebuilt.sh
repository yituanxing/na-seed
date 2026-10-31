#!/bin/sh
# M140: signed Alpine GNU Coreutils; no in-guest compilation or installer.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# New means historically uncounted, not a BusyBox alias or duplicate SHA.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = scripts/m140-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M140 DUPLICATE ORIGINAL ELF] $elf previously in $old" >&2
            exit 1
        fi
    done
done < scripts/m140-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m140-batch
work="build/external/m140-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:coreutils' "$root/lib/apk/db/installed" || exit 1
root_canonical=$(cd "$root" && pwd -P)
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$elf") || exit 1
    case "$target" in "$root_canonical"/*) ;;
        *) echo "[M140 HOST STAGE FAIL] ELF escapes signed guest root: $elf -> $target" >&2
           exit 1 ;;
    esac
    case "$target" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M140 HOST STAGE FAIL] BusyBox alias: $elf" >&2
        exit 1 ;;
    esac
    test -x "$target" || exit 1
    sha256sum "$target"
    count=$((count + 1))
done < scripts/m140-original-elfs.txt > "$work/m140-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m140-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 20 ] || [ "$unique" -ne 20 ]; then
    echo "[M140 HOST STAGE FAIL] $port $count paths/$unique unique SHA" >&2
    exit 1
fi
echo "[M140 HOST STAGE PASS] $port 20 distinct signed GNU Coreutils ELFs"
