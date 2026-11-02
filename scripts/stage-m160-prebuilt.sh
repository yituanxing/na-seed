#!/bin/sh
# M160: existing original signed Alpine clients + original image consumers.
# One canonical APK overlay owns the CPIO. Host private TLS keys stay outside.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m160-original-elfs.txt
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    found=0
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then found=$((found + 1)); fi
    done
    [ "$found" -gt 0 ] || {
        echo "[M160 INVENTORY FAIL] not historically admitted: $elf" >&2
        exit 1
    }
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 6 ] || exit 1

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m160-batch
work="build/external/m160-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for package in curl wget netpbm chafa; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
        echo "[M160 STAGE FAIL] unsigned/missing APK $port $package" >&2
        exit 1
    }
done
for fixture in tests/m160-image.sha256 tests/m156-https-ca.pem \
               tests/m160-batch-run.sh; do
    test -s "$root/$fixture" || {
        echo "[M160 STAGE FAIL] missing guest fixture $fixture" >&2
        exit 1
    }
done
test -s "$work/host-m160-image.png"
test -s "$work/host-ca.crt" && test -s "$work/host-tls.crt"
test -s "$work/host-ca.key" && test -s "$work/host-tls.key"
test ! -e "$root/host-ca.key" && test ! -e "$root/host-tls.key"
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        echo "[M160 STAGE FAIL] BusyBox alias: $elf" >&2; exit 1 ;;
    esac
    test -x "$resolved" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M160 STAGE FAIL] not original ELF: $elf" >&2; exit 1
    }
    sha256sum "$resolved"
done < "$manifest" > "$work/m160-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m160-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 6 ] || exit 1
echo "[M160 APK STAGE PASS] $port six already-counted native signed ELF clients and image consumers"
