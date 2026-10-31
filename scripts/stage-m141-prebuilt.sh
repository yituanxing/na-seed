#!/bin/sh
# M141: host-only signed APK staging of 68 distinct original Alpine ELFs.
# This counts no guest PASS; QEMU semantic probes are a separate mandatory gate.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

manifest=scripts/m141-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M141 DUPLICATE HISTORICAL ELF PATH] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 68 ] || { echo "[M141 STAGE FAIL] $count paths, expected 68" >&2; exit 1; }

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m141-batch
work="build/external/m141-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
root_canonical=$(cd "$root" && pwd -P)

while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$elf") || exit 1
    case "$target" in "$root_canonical"/*) ;;
        *) echo "[M141 STAGE FAIL] escaped signed root: $elf -> $target" >&2; exit 1 ;;
    esac
    case "$target" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M141 STAGE FAIL] BusyBox alias: $elf" >&2
        exit 1 ;;
    esac
    test -f "$target" && test -x "$target" || exit 1
    sha256sum "$target"
done < "$manifest" > "$work/m141-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m141-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 68 ] || {
    echo "[M141 STAGE FAIL] $port 68 paths but only $unique unique native ELF SHA256" >&2
    exit 1
}
echo "[M141 SIGNED APK STAGE PASS] $port 68 different original target ELFs"
echo '[M141 NOT ACCEPTED] native QEMU semantic checks still required'
