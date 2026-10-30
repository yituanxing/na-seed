#!/bin/sh
# Second software batch: only the canonical signed Alpine APK host overlay.
# Neither the original applications nor their dependencies build in NaSeed.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: stage-m109-prebuilt.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m109-batch
work="build/external/m109-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
# The overlay already verified APK signatures and target machine for every
# listed ELF. Keep a per-ISA inventory of their exact unmodified bytes.
count=0
while IFS= read -r guest_elf; do
    case "$guest_elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$guest_elf"
    count=$((count + 1))
done < scripts/m109-original-elfs.txt > "$work/m109-original-signed-elfs.sha256"
[ "$count" -eq 15 ] && test -s "$work/m109-original-signed-elfs.sha256" || {
    echo "[M109 HOST STAGE FAIL] $port expected 15 signed native executable hashes, got $count" >&2
    exit 1
}
echo "[M109 HOST STAGE PASS] $port 15 original signed Alpine APK ELFs; no guest compiler"
