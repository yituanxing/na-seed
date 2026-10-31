#!/bin/sh
# M138: twenty new signed original LLVM 21 compiler-toolchain executables.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# The historical target list is append-only: no reused target paths.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = scripts/m138-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M138 DUPLICATE ORIGINAL ELF] $elf previously in $old" >&2
            exit 1
        fi
    done
done < scripts/m138-original-elfs.txt

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m138-batch
work="build/external/m138-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:llvm21' "$root/lib/apk/db/installed" || {
    echo "[M138 HOST STAGE FAIL] $port missing signed llvm21" >&2
    exit 1
}
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$elf") || exit 1
    case "$target" in "$root/bin/busybox"|"$root/usr/bin/busybox")
        echo "[M138 HOST STAGE FAIL] BusyBox alias $elf" >&2
        exit 1 ;;
    esac
    test -x "$target"
    sha256sum "$target"
    count=$((count + 1))
done < scripts/m138-original-elfs.txt > "$work/m138-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m138-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 20 ] || [ "$unique" -ne 20 ]; then
    echo "[M138 HOST STAGE FAIL] $port only $count paths/$unique distinct signed SHA256" >&2
    exit 1
fi
echo "[M138 HOST STAGE PASS] $port 20 new distinct signed LLVM 21 ELFs"
