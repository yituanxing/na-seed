#!/bin/sh
# M130: verify 15 genuinely distinct original signed archive/image/metadata ELFs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac

# Historical ELF paths, not just the latest batch, belong to one census.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    for manifest in scripts/m[0-9]*-original-elfs.txt; do
        [ "$manifest" = scripts/m130-original-elfs.txt ] && continue
        if grep -Fxq "$elf" "$manifest"; then
            echo "[M130 DUPLICATE ORIGINAL ELF] $elf already in $manifest" >&2
            exit 1
        fi
    done
done < scripts/m130-original-elfs.txt

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
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$root/bin/busybox"|"$root/usr/bin/busybox")
            echo "[M130 HOST STAGE FAIL] BusyBox alias: $elf" >&2
            exit 1 ;;
    esac
    # Hash the referenced ELF content, not a symlink pathname.
    sha256sum "$root/$elf"
    count=$((count + 1))
done < scripts/m130-original-elfs.txt > "$work/m130-original-signed-elfs.sha256"
unique=$(awk '{ print $1 }' "$work/m130-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique" -ne 15 ]; then
    echo "[M130 HOST STAGE FAIL] $port $count paths, $unique distinct original SHA256" >&2
    exit 1
fi
echo "[M130 HOST STAGE PASS] $port 15 globally new signed archive/image/metadata ELFs"
