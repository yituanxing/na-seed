#!/bin/sh
# New original Alpine Netpbm conversions/filters, signed on each native ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# Every milestone owns globally new executable PATHS; same-program aliases,
# previous milestones and within-batch SHA duplicates are not new targets.
sh scripts/acceptance/check-elf-history.sh M131 scripts/m131-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m131-batch
work="build/external/m131-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed" || {
    echo "[M131 HOST STAGE FAIL] $port unsigned/missing netpbm" >&2
    exit 1
}
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m131-original-elfs.txt "$work/m131-original-signed-elfs.sha256" 15 alias-exec M131
echo "[M131 HOST STAGE PASS] $port 15 new signature-verified original Netpbm ELFs"
