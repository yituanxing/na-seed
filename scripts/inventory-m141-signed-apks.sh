#!/bin/sh
# Host-only M141 candidate census; this does NOT run or accept guest programs.
# Usage: sh scripts/inventory-m141-signed-apks.sh amd64|riscv64|arm64
# Download official signed Alpine 3.23 APKs once and classify candidate paths.
set -eu

[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) arch=riscv64; machine='RISC-V' ;;
    arm64) arch=aarch64; machine='AArch64' ;;
    *) echo '[M141 CANDIDATE ERROR] unknown ISA' >&2; exit 2 ;;
esac

python3 scripts/check-m141-candidates.py
command -v docker >/dev/null || { echo '[M141 CANDIDATE ERROR] docker required' >&2; exit 2; }
command -v readelf >/dev/null || { echo '[M141 CANDIDATE ERROR] readelf required' >&2; exit 2; }

manifest=scripts/m141-candidate-elfs.tsv
packages=rootfs/alpine/packages-m141-candidates.txt
work="build/external/m141-candidate-inventory/$port"
package_sha=$(sha256sum "$packages" | awk '{print $1}')
root="$work/packages-$package_sha/rootfs"
# Unlike the normal overlay, this discovery root starts empty. apk --root
# expects a writable package database directory before its first install.
mkdir -p "$root/lib/apk/db" "$root/etc/apk"
root_canonical=$(cd "$root" && pwd -P)
package_list=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$packages")
test -n "$package_list"

# Only reuse a root created for the identical package set and port.
# The official APK signature check remains on for all packages.
if [ ! -f "$work/packages-$package_sha/signed-install.ok" ]; then
    if ! docker run --rm --volume "$root_canonical:/target" \
        --env APK_ARCH="$arch" --env APK_PACKAGES="$package_list" \
        alpine:3.23 sh -ec '
            apk --arch "$APK_ARCH" --root /target --initdb \
                --keys-dir /etc/apk/keys \
                --no-scripts \
                -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
                -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
                add $APK_PACKAGES
        ' > "$work/apk-install.log" 2>&1; then
        echo "[M141 CANDIDATE APK FAIL] $port" >&2
        tail -100 "$work/apk-install.log" >&2
        exit 1
    fi
    touch "$work/packages-$package_sha/signed-install.ok"
fi

report="$work/candidates.tsv"
printf 'package\telf_path\tstatus\tsha256\n' > "$report"
tab=$(printf '\t')
tail -n +2 "$manifest" | while IFS="$tab" read -r package path family probe; do
    case "$package" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$path" || true)
    status=MISSING
    sha=-
    case "$target" in
        "$root_canonical"/*)
            if [ -f "$target" ] && [ -x "$target" ]; then
                if readelf -h "$target" 2>/dev/null | grep -Fq "$machine"; then
                    sha=$(sha256sum "$target" | awk '{print $1}')
                    status=NATIVE_ELF
                elif readelf -h "$target" >/dev/null 2>&1; then
                    status=WRONG_ISA
                else
                    status=NOT_ELF
                fi
            fi ;;
        '') status=MISSING ;;
        *) status=ESCAPES_ROOT ;;
    esac
    printf '%s\t%s\t%s\t%s\n' "$package" "$path" "$status" "$sha" >> "$report"
done

native=$(awk -F '\t' '$3 == "NATIVE_ELF" { n++ } END { print n+0 }' "$report")
unique=$(awk -F '\t' '$3 == "NATIVE_ELF" { print $4 }' "$report" | sort -u | wc -l)
total=$(awk 'END {print NR-1}' "$report")
printf '[M141 CANDIDATE INVENTORY] %s: %s/%s ELF paths, %s unique SHA\n' \
    "$port" "$native" "$total" "$unique"
awk -F '\t' 'NR > 1 && $3 != "NATIVE_ELF" { print "[M141 FILTER] " $2 " " $3 }' "$report"
echo '[M141 NOT ACCEPTED] native QEMU semantics and cross-ISA SHA gates pending'
