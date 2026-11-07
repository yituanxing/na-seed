#!/bin/sh
# M150: 26 genuinely new original signed Alpine v3.23 ELF identities.
# The shared host APK installer verifies Alpine signatures/architecture.
# This gate additionally rejects old milestone paths, BusyBox aliases and
# duplicate actual SHA256 content, never counting symlinks as new software.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m150-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M150 DUPLICATE PATH] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M150 HISTORIC DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 26 ] || {
    echo "[M150 STAGE FAIL] expected 26 new paths, got $count" >&2; exit 1;
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m150-batch
work="build/external/m150-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in lrzip par2cmdline pixz plzip sleuthkit; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M150 STAGE FAIL] missing signed Alpine package $package" >&2
        exit 1
    }
done
for fixture in m150-ext2.img m150-mbr.img m150-input.txt; do
    test -s "$root/tests/$fixture" || exit 1
done
root_canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$root_canonical"/*) ;; *)
        echo "[M150 STAGE FAIL] $elf escaped signed root" >&2; exit 1 ;;
    esac
    case "$resolved" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M150 STAGE FAIL] BusyBox alias $elf" >&2; exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M150 STAGE FAIL] non-ELF $elf ($magic)" >&2; exit 1;
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m150-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m150-original-signed-elfs.sha256" |
         sort -u | wc -l)
[ "$unique" -eq 26 ] || {
    echo "[M150 STAGE FAIL] $port $unique unique SHA256, require 26" >&2
    exit 1
}
echo "[M150 SIGNED APK STAGE PASS] $port 26 distinct original ELF SHA256"
echo '[M150 QEMU NOT YET ACCEPTED] guest semantic proof pending native run'
