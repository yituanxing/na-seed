#!/bin/sh
# M130: verify 15 genuinely distinct original signed archive/image/metadata ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

# Historical ELF paths, not just the latest batch, belong to one census.
sh scripts/acceptance/check-elf-history.sh M130 scripts/m130-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m130-batch
work="build/external/m130-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in netpbm binutils libarchive-tools zip attr acl e2fsprogs-extra; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M130 HOST STAGE FAIL] $port missing signed $package" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m130-original-elfs.txt "$work/m130-original-signed-elfs.sha256" 15 alias-path M130
echo "[M130 HOST STAGE PASS] $port 15 globally new signed archive/image/metadata ELFs"
