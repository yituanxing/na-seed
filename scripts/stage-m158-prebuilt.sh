#!/bin/sh
# Thirty historically new Alpine Netpbm ELF codec identities in three ISAs.
# Signed APK installation, ISA verification and canonical CPIO have one owner.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m158-original-elfs.txt
count=$(sh scripts/acceptance/check-elf-history.sh M158 "$manifest" --count --distinct)
[ "$count" -eq 30 ] || {
    echo "[M158 INVENTORY FAIL] need 30 native ELF paths, got $count" >&2
    exit 1
}

sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m158-batch
work="build/external/m158-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq "P:netpbm" "$root/lib/apk/db/installed" || {
    echo "[M158 STAGE FAIL] no signed Netpbm APK" >&2
    exit 1
}
for fixture in m158-color.ppm m158-palette.ppm m158-gray.pgm m158-bw.pbm; do
    test -s "$root/tests/$fixture" || {
        echo "[M158 STAGE FAIL] missing image: $fixture" >&2
        exit 1
    }
done
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/m158-original-signed-elfs.sha256" 30 contained-elf M158
echo "[M158 APK STAGE PASS] $port 30 distinct original signed Alpine ELF hashes"
echo '[M158 GUEST SEMANTICS PENDING] native QEMU is the separate execution gate'
