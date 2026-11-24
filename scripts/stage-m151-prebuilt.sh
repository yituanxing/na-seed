#!/bin/sh
# 35 never-before-counted original signed Alpine multimedia ELF identities.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m151-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M151 "$manifest" --count --distinct)
[ "$count" -eq 35 ] || {
    echo "[M151 STAGE FAIL] $count paths, require 35" >&2; exit 1;
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m151-batch
work="build/external/m151-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in libsndfile aubio heif-thumbnailer faad2 fdkaac audiofile rubberband \
               soundtouch aom libvpx-utils libjxl-tools libheif-tools jasper netpbm; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M151 STAGE FAIL] missing signed package $package" >&2
        exit 1
    }
done
for fixture in m151-mono.wav m151-stereo.wav m151-beats.wav m151-source.png \
               m151-source.ppm m151-source.y4m; do
    test -s "$root/tests/$fixture" || exit 1
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m151-original-signed-elfs.sha256" 35 contained-elf M151
echo "[M151 SIGNED APK STAGE PASS] $port 35 new distinct original ELF SHA256"
echo '[M151 NATIVE QEMU UNVERIFIED] exact guest semantic markers still required'
