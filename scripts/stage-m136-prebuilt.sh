#!/bin/sh
# Twenty signed original parser, ncurses and util-linux ELFs on each ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/acceptance/check-elf-history.sh M136 scripts/m136-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m136-batch
work="build/external/m136-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in flex byacc re2c ncurses ncurses-terminfo-base util-linux-misc \
           uuidgen findmnt flock lscpu; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M136 HOST STAGE FAIL] $port unsigned/missing $pkg" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m136-original-elfs.txt "$work/m136-original-signed-elfs.sha256" 20 alias-exec M136
echo "[M136 HOST STAGE PASS] $port 20 newly signed and distinct ELF files"
