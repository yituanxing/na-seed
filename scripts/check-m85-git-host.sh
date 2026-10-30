#!/bin/sh
# Fast Linux semantic oracle + raw-image inspector contract. This DOES NOT
# replace the three real QEMU guest boots; it tests only the host-side oracle.
set -eu
[ "$#" -eq 0 ] || exit 2
for binary in git mke2fs truncate debugfs e2fsck; do
    command -v "$binary" >/dev/null || {
        echo "[M85 HOST FAIL] missing $binary" >&2
        exit 1
    }
done
temp=$(mktemp -d)
trap 'case "$temp" in /tmp/*) rm -rf -- "$temp" ;; esac' EXIT
mkdir -p "$temp/root" "$temp/evidence"
sh tests/m85-git-run.sh --host-first "$temp/root"
truncate -s 256M "$temp/evidence/physical-fixture.img"
mke2fs -q -F -t ext4 -b 4096 -O '^metadata_csum,^64bit' \
    -d "$temp/root" "$temp/evidence/physical-fixture.img"
sh scripts/check-m85-physical-git.sh first \
    "$temp/evidence/physical-fixture.img" "$temp/evidence" host
sh tests/m85-git-run.sh --host-replay "$temp/root"
# The synthetic image is intentionally not rebuilt here. Actual two-boot
# persistence can only be established by scripts/probe-m85-git.sh in QEMU.
sh scripts/check-m85-physical-git.sh replay \
    "$temp/evidence/physical-fixture.img" "$temp/evidence" host
echo '[M85 HOST PASS] original Linux Git semantics and raw ext4 verifier; QEMU still required'
