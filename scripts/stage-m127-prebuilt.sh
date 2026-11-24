#!/bin/sh
# Signed Alpine audio original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/acceptance/check-elf-history.sh M127 scripts/m127-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m127-batch
work="build/external/m127-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in wavpack vorbis-tools speex-tools flac lame mpg123; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M127 HOST STAGE FAIL] $port signed APK missing $package" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m127-original-elfs.txt "$work/m127-original-signed-elfs.sha256" 15 alias-path M127
echo "[M127 HOST STAGE PASS] $port 15 globally new signed audio ELF programs"
