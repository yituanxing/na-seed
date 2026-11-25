#!/bin/sh
# Stage the distinct signed ELF inventories for the early network cohorts.
# Guest protocol proofs remain in their individual acceptance runners.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
cohort=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$cohort" in
    m143) expected=3; output=m143-original-signed-elfs.sha256 ;;
    m144) expected=5; output=m144-original-elfs.sha256 ;;
    m147) expected=29; output=m147-original-elfs.sha256 ;;
    m149) expected=35; output=m149-original-elfs.sha256 ;;
    m152) expected=35; output=m152-original-elfs.sha256 ;;
    *) exit 2 ;;
esac
prefix=$(printf '%s' "$cohort" | tr m M)
manifest="scripts/$cohort-original-elfs.txt"
count=$(sh scripts/acceptance/check-elf-history.sh "$prefix" "$manifest" --count)
if [ "$count" -ne "$expected" ]; then
    case "$cohort" in
        m147) echo "[M147 FAIL] inventory count=$count" >&2 ;;
        m149) echo "[M149 FAIL] 35 required, found $count" >&2 ;;
        m152) echo "[M152 FAIL] expected 35, got $count" >&2 ;;
    esac
    exit 1
fi
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" "$cohort-batch"
root="build/external/$cohort-batch/$port"
# All cohorts require a nonempty CPIO. M144 historically has no separate APK DB assertion.
case "$cohort" in
    m144) test -s "$root/rootfs.cpio" ;;
    *) test -s "$root/rootfs.cpio"; test -s "$root/rootfs/lib/apk/db/installed" ;;
esac
sh scripts/acceptance/hash-original-elfs.sh "$root/rootfs" "$manifest" "$root/$output" "$expected" contained-file "$prefix"
case "$cohort" in
    m143) echo "[M143 STAGED] $port three unique signed native Alpine ELFs; QEMU pending" ;;
    m144) echo '[M144 STAGED] five different original signed Alpine executables' ;;
    m147) echo '[M147 STAGED] 29 genuinely new signed Alpine ELF identities; QEMU pending' ;;
    m149) echo '[M149 STAGED] 35 new independently signed Alpine ELF identities; QEMU pending' ;;
    m152) echo '[M152 STAGED] 35 original signed Alpine ELF identities; native QEMU pending' ;;
esac
