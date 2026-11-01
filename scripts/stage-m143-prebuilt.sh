#!/bin/sh
# Only stage signed Alpine binaries on host; native QEMU semantic tests are separate.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m143-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M143 HISTORICAL DUPLICATE] $elf already in $old" >&2
            exit 1
        fi
    done
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 3 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m143-batch
root="build/external/m143-batch/$port"
test -s "$root/rootfs.cpio"
test -s "$root/rootfs/lib/apk/db/installed"
root_canonical=$(cd "$root/rootfs" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    real=$(readlink -f "$root/rootfs/$elf") || exit 1
    case "$real" in "$root_canonical"/*) ;; *) exit 1 ;; esac
    test -f "$real" && test -x "$real"
    case "$real" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox") exit 1 ;; esac
    sha256sum "$real"
done < "$manifest" > "$root/m143-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$root/m143-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 3 ] || { echo "[M143 STAGE FAIL] duplicate binary hashes" >&2; exit 1; }
echo "[M143 STAGED] $port three unique signed native Alpine ELFs; QEMU pending"
