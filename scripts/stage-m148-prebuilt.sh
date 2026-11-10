#!/bin/sh
# 35 distinct ORIGINAL Alpine 3.23 native Netpbm ELFs, host-only signed staging.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m148-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M148 HISTORICAL ELF DUPLICATE] $elf already in $old" >&2
            exit 1
        fi
    done
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 35 ] || { echo "[M148 STAGE FAIL] 35 expected ELF paths, got $count" >&2; exit 1; }

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m148-batch
work="build/external/m148-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed" || exit 1
root_canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$root_canonical"/*) ;; *)
        echo "[M148 STAGE FAIL] escaped signed root: $elf -> $resolved" >&2
        exit 1 ;;
    esac
    case "$resolved" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M148 STAGE FAIL] BusyBox alias $elf" >&2
        exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M148 STAGE FAIL] not a native ELF: $elf ($magic)" >&2
        exit 1
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m148-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m148-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 35 ] || {
    echo "[M148 STAGE FAIL] $port 35 paths but only $unique unique signed SHA256" >&2
    exit 1
}
echo "[M148 SIGNED APK STAGE PASS] $port 35 original distinct ELF SHA256"
echo '[M148 NOT QEMU ACCEPTED] Native guest semantic jobs still required'
