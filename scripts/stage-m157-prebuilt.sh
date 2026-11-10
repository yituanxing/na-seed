#!/bin/sh
# M157: 30 original signed and historically new Alpine image utilities.
# This host import uses exactly the shared APK signature/ISA authority.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m157-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M157 DUPLICATE PATH] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M157 HISTORICAL DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 30 ] || {
    echo "[M157 INVENTORY FAIL] need 30 native ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m157-batch
work="build/external/m157-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in netpbm giflib-utils tiff-tools libpng-utils; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M157 STAGE FAIL] unsigned/missing package $port $package" >&2
        exit 1
    }
done
for fixture in m157-color.ppm m157-bw.pbm m157-gray.pgm m157-color.png \
               m157-bad-itxt.png m157-color.gif m157-paletted.tif m157-fax.raw; do
    test -s "$root/tests/$fixture" || {
        echo "[M157 STAGE FAIL] missing input $fixture" >&2
        exit 1
    }
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *)
        echo "[M157 STAGE FAIL] ELF escapes root: $elf" >&2; exit 1 ;;
    esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        echo "[M157 STAGE FAIL] BusyBox alias: $elf" >&2; exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M157 STAGE FAIL] non-ELF original: $elf" >&2; exit 1
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m157-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m157-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 30 ] || {
    echo "[M157 STAGE FAIL] $port only $unique distinct images, need 30" >&2
    exit 1
}
echo "[M157 APK STAGE PASS] $port 30 distinct original signed Alpine ELF hashes"
echo '[M157 GUEST SEMANTICS PENDING] native QEMU acceptance is a separate gate'
