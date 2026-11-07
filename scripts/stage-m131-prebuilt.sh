#!/bin/sh
# New original Alpine Netpbm conversions/filters, signed on each native ISA.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# Every milestone owns globally new executable PATHS; same-program aliases,
# previous milestones and within-batch SHA duplicates are not new targets.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = scripts/m131-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M131 DUPLICATE ORIGINAL ELF] $elf in $old" >&2
            exit 1
        fi
    done
done < scripts/m131-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m131-batch
work="build/external/m131-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:netpbm' "$root/lib/apk/db/installed" || {
    echo "[M131 HOST STAGE FAIL] $port unsigned/missing netpbm" >&2
    exit 1
}
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$elf") || exit 1
    case "$target" in "$root/bin/busybox"|"$root/usr/bin/busybox")
        echo "[M131 HOST STAGE FAIL] multicall BusyBox alias $elf" >&2
        exit 1 ;;
    esac
    test -x "$target"
    sha256sum "$target"
    count=$((count + 1))
done < scripts/m131-original-elfs.txt > "$work/m131-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m131-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M131 HOST STAGE FAIL] $port only $count paths/$unique distinct signed SHA256" >&2
    exit 1
fi
echo "[M131 HOST STAGE PASS] $port 15 new signature-verified original Netpbm ELFs"
