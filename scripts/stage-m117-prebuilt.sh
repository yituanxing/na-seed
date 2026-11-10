#!/bin/sh
# Host-stage 15 independent signed Alpine native ELFs; no guest compilation.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: stage-m117-prebuilt.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac

# Canonical overlay resolves and signature-verifies APKs on Linux host,
# enforces correct ELF machine, then packs the guest initramfs.
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m117-batch
work="build/external/m117-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"

count=0
while IFS= read -r guest_elf; do
    case "$guest_elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$guest_elf"
    count=$((count + 1))
done < scripts/m117-original-elfs.txt > "$work/m117-original-signed-elfs.sha256"

unique_hashes=$(awk '{ print $1 }' "$work/m117-original-signed-elfs.sha256" | sort -u | wc -l)
if [ "$count" -ne 15 ] || [ "$unique_hashes" -ne 15 ] ||
   ! test -s "$work/m117-original-signed-elfs.sha256"; then
    echo "[M117 HOST STAGE FAIL] $port expected 15 distinct signed native ELFs, got $count paths and $unique_hashes unique hashes" >&2
    exit 1
fi
echo "[M117 HOST STAGE PASS] $port 15 distinct signed Alpine 3.23 native ELFs; zero guest builds"
