#!/bin/sh
# Signed Alpine groff original ELF programs; host-only APK install.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

sh scripts/acceptance/check-elf-history.sh M126 scripts/m126-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m126-batch
work="build/external/m126-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:groff' "$root/lib/apk/db/installed"
# GNU groff preprocessors and output drivers depend on their original font,
# macro, and device definitions; a 15-binary-only rootfs cannot typeset text.
for resource in font tmac; do
    dir="$root/usr/share/groff/1.23.0/$resource"
    if ! test -d "$dir" || ! find "$dir" -type f -print -quit | grep -q .; then
        echo "[M126 HOST STAGE FAIL] missing signed groff resource directory: $dir" >&2
        exit 1
    fi
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m126-original-elfs.txt "$work/m126-original-signed-elfs.sha256" 15 alias-path M126
echo "[M126 HOST STAGE PASS] $port 15 globally new native groff original ELFs"
