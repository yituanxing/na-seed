#!/bin/sh
# Twenty signed original DB, compiler, font, archive, media and backup ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/acceptance/check-elf-history.sh M137 scripts/m137-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m137-batch
work="build/external/m137-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in lmdb-tools fontconfig font-dejavu protoc flatc swig ragel \
           xorriso graphicsmagick mdbook rclone restic recode fdupes \
           zint zbar; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M137 HOST STAGE FAIL] $port unsigned/missing $pkg" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m137-original-elfs.txt "$work/m137-original-signed-elfs.sha256" 20 alias-exec M137
echo "[M137 HOST STAGE PASS] $port 20 newly signed and distinct ELF files"
