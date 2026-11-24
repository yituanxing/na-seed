#!/bin/sh
# Five original Alpine software families; 15 new, distinct signed ELF files.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

sh scripts/acceptance/check-elf-history.sh M132 scripts/m132-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m132-batch
work="build/external/m132-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in icu libwebp-tools zopfli pcre2-tools bzip3; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M132 HOST STAGE FAIL] $port unsigned/missing $pkg" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m132-original-elfs.txt "$work/m132-original-signed-elfs.sha256" 15 alias-exec M132
echo "[M132 HOST STAGE PASS] $port 15 new signature-verified original ELF files"
