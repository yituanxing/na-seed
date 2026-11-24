#!/bin/sh
# Host-only signed APK import: 30 genuinely NEW native ELF identities.
# No substitution, compiling, or ABI emulation inside the NaSeed guest.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m154-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M154 "$manifest" --count)
[ "$count" -eq 30 ] || {
    echo "[M154 STAGE FAIL] 30 ELF paths expected; found $count" >&2; exit 1;
}
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m154-batch
work="build/external/m154-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in a2ps barcode chafa csview csvq enca enscript figlet \
               gnuplot ghostscript html2text jql libxml2-utils mandoc \
               pango-tools potrace psutils rsvg-convert taplo unrtf font-dejavu; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M154 STAGE FAIL] $port missing signed package $package" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m154-original-signed-elfs.sha256" 30 contained-elf M154
echo "[M154 SIGNED APK STAGE PASS] $port 30 unique original ELF SHA256"
echo '[M154 NATIVE QEMU PENDING] guest semantics still require actual acceptance'
