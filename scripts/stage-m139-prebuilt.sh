#!/bin/sh
# M139: twenty original signed font, image, process, crypto ELF programs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# Historical ELF paths are append-only; neither earlier nor later batches
# may silently count one M139 executable as a newly ported target.
sh scripts/acceptance/check-elf-history.sh M139 scripts/m139-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m139-batch
work="build/external/m139-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in fontconfig font-dejavu netpbm util-linux-misc mcookie \
           gnutls-utils gnupg-gpgconf; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M139 HOST STAGE FAIL] $port missing signed $pkg" >&2
        exit 1
    }
done

sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m139-original-elfs.txt "$work/m139-original-signed-elfs.sha256" 20 contained-exec M139
echo "[M139 HOST STAGE PASS] $port 20 new distinct signed original ELFs"
