#!/bin/sh
# One signed-original ELF per PDF CLI program, no guest compilation.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

# Close a gap found in M120: a *new* application cannot duplicate any older
# original-ELF manifest.  Historical re-runs remain possible in old profiles.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m121-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M121 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m121-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m121-batch
work="build/external/m121-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for fixture in m121-original.pdf m121-invalid.pdf m121-attachment.txt; do
    test -s "$root/tests/$fixture" || exit 1
done
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m121-original-elfs.txt > "$work/m121-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m121-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M121 HOST STAGE FAIL] $port $count original programs, $unique unique SHA" >&2
    exit 1
fi
echo "[M121 HOST STAGE PASS] $port 15 globally new signed native PDF ELFs, host-only fixtures"
