#!/bin/sh
# One signed-original ELF per PDF CLI program, no guest compilation.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

# Close a gap found in M120: a *new* application cannot duplicate any older
# original-ELF manifest.  Historical re-runs remain possible in old profiles.
sh scripts/acceptance/check-elf-history.sh M121 scripts/m121-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m121-batch
work="build/external/m121-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for fixture in m121-original.pdf m121-invalid.pdf m121-attachment.txt; do
    test -s "$root/tests/$fixture" || exit 1
done
sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m121-original-elfs.txt "$work/m121-original-signed-elfs.sha256" 15 unique M121
echo "[M121 HOST STAGE PASS] $port 15 globally new signed native PDF ELFs, host-only fixtures"
