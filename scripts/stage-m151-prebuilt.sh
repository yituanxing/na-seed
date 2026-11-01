#!/bin/sh
# 35 never-before-counted original signed Alpine multimedia ELF identities.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m151-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*) echo "[M151 DUPLICATE PATH] $elf" >&2; exit 1 ;; esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M151 HISTORICAL ELF DUPLICATE] $elf in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 35 ] || {
    echo "[M151 STAGE FAIL] $count paths, require 35" >&2; exit 1;
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m151-batch
work="build/external/m151-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in libsndfile aubio faac faad2 fdkaac audiofile rubberband \
               soundtouch aom libvpx-utils libjxl-tools libheif-tools jasper netpbm; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M151 STAGE FAIL] missing signed package $package" >&2
        exit 1
    }
done
for fixture in m151-mono.wav m151-stereo.wav m151-source.png \
               m151-source.ppm m151-source.y4m; do
    test -s "$root/tests/$fixture" || exit 1
done
root_canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$root_canonical"/*) ;; *)
        echo "[M151 STAGE FAIL] ELF escapes signed root: $elf" >&2; exit 1 ;; esac
    case "$resolved" in "$root_canonical/bin/busybox"|"$root_canonical/usr/bin/busybox")
        echo "[M151 STAGE FAIL] BusyBox alias: $elf" >&2; exit 1 ;; esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M151 STAGE FAIL] not ELF: $elf" >&2; exit 1; }
    sha256sum "$resolved"
done < "$manifest" > "$work/m151-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m151-original-signed-elfs.sha256" |
         sort -u | wc -l)
[ "$unique" -eq 35 ] || {
    echo "[M151 STAGE FAIL] only $unique distinct signed SHA256" >&2; exit 1; }
echo "[M151 SIGNED APK STAGE PASS] $port 35 new distinct original ELF SHA256"
echo '[M151 NATIVE QEMU UNVERIFIED] exact guest semantic markers still required'
