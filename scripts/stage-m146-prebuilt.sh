#!/bin/sh
# Stage 24 NEW original signed Alpine 3.23 ELFs; host-only apk installation.
# Staging is a provenance gate, not a claim of successful native QEMU execution.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m146-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M146 DUPLICATE ORIGINAL ELF] $elf previously in $old" >&2
            exit 1
        fi
    done
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 24 ] || { echo "[M146 STAGE FAIL] expected 24 ELF paths, found $count" >&2; exit 1; }

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m146-batch
work="build/external/m146-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in netpbm imagemagick php84 pax-utils scanelf; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M146 STAGE FAIL] missing original signed package $package" >&2
        exit 1
    }
done
root_canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$root_canonical"/*) ;; *)
        echo "[M146 STAGE FAIL] $elf escapes signed root: $resolved" >&2
        exit 1 ;;
    esac
    case "$resolved" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M146 STAGE FAIL] BusyBox alias $elf" >&2
        exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M146 STAGE FAIL] non-ELF $elf ($magic)" >&2
        exit 1
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m146-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m146-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 24 ] || {
    echo "[M146 STAGE FAIL] $port 24 paths but only $unique unique original SHA256" >&2
    exit 1
}
echo "[M146 SIGNED APK STAGE PASS] $port 24 distinct original ELF SHA256"
echo '[M146 QEMU NOT YET ACCEPTED] native guest behavior still requires validation'
