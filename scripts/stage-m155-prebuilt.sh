#!/bin/sh
# 33 globally new signed Alpine v3.23 executables. The canonical overlay
# installer validates Alpine repository keys, APK architecture and signatures;
# here we additionally reject aliases, historic paths and duplicate bytes.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m155-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M155 "$manifest" --count --distinct)
[ "$count" -eq 33 ] || {
    echo "[M155 STAGE FAIL] require 33 new ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m155-batch
work="build/external/m155-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in btrfs-progs btrfs-progs-extra xfsprogs xfsprogs-extra \
               f2fs-tools erofs-utils ntfs-3g-progs gptfdisk parted; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M155 STAGE FAIL] missing signed Alpine package $package" >&2
        exit 1
    }
done
for kind in btrfs xfs f2fs ntfs gpt; do
    gzip -t "$root/tests/m155-$kind.img.gz"
done
test -s "$root/tests/m155-erofs.img" &&
test -s "$root/tests/m155-input.txt" &&
test -s "$root/tests/m155-seed/evidence.txt"
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m155-original-signed-elfs.sha256" 33 contained-elf M155
echo "[M155 SIGNED APK STAGE PASS] $port 33 distinct original ELF SHA256, compact real filesystem fixtures"
echo '[M155 QEMU NOT YET ACCEPTED] guest semantic proof pending native run'
