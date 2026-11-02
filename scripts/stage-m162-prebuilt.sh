#!/bin/sh
# One signed Alpine profile, seven previously admitted real Linux executables
# and only host-generated ephemeral TLS identity. No guest-built substitutes.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
    *) exit 2 ;;
esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m162-batch
work="build/external/m162-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
for pkg in curl wget netpbm chafa imagemagick fbgrab; do
    grep -Fxq "P:$pkg" "$root/lib/apk/db/installed" || {
        echo "[M162 SIGNED APK FAIL] $port $pkg" >&2
        exit 1
    }
done
canonical=$(cd "$root" && pwd -P)
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        echo "[M162 ELF FAIL] BusyBox alias: $elf" >&2; exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || exit 1
    readelf -h "$resolved" | grep -Fq "$machine" || exit 1
    magic=$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')
    [ "$magic" = 7f454c46 ] || exit 1
    sha256sum "$resolved"
    count=$((count+1))
done < scripts/m162-reused-elfs.txt > "$work/m162-original-signed-elfs.sha256"
[ "$count" -eq 7 ] || exit 1
[ "$(awk '{print $1}' "$work/m162-original-signed-elfs.sha256" | sort -u | wc -l)" -eq 7 ] || exit 1
for fixture in tests/m162-batch-run.sh tests/m162-wire-image.sha256 \
               tests/m156-https-ca.pem; do
    test -s "$root/$fixture" || exit 1
done
test -s "$work/host-m162-display.png"
test ! -e "$root/host-m162-display.png"
test -s "$work/host-ca.key" && test -s "$work/host-tls.key"
test ! -e "$root/host-ca.key" && test ! -e "$root/host-tls.key"
echo "[M162 SIGNED STAGE PASS] $port 7 authentic existing ELFs, host-only PNG and keys"
