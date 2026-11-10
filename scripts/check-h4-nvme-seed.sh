#!/bin/sh
# Deterministic, disposable, filesystem-free QEMU NVMe namespace fixture.
# This is the ONLY image path H4 accepts. Never take an external disk path.
set -eu

image=build/h4-qemu-only/h4-seed.img
expected_sha=2ec2216d17e0a98425c45853c7cc74846e899b19d78659b4c44df74c632a3207

[ ! -L build ] && [ ! -L build/h4-qemu-only ] || {
    echo 'H4: refusing linked build directory' >&2
    exit 2
}
if [ -e "$image" ] || [ -L "$image" ]; then
    [ -f "$image" ] && [ ! -L "$image" ] || {
        echo 'H4: refusing existing non-regular/symlink fixture' >&2
        exit 2
    }
    actual=$(sha256sum "$image" | awk '{print $1}')
    if [ "$actual" != "$expected_sha" ]; then
        echo 'H4: refusing to overwrite a pre-existing unrecognized fixture' >&2
        exit 2
    fi
    echo "H4 seed verified: $actual"
    exit 0
fi

mkdir -p build/h4-qemu-only
dd if=/dev/zero of="$image" bs=1048576 count=1 status=none

# 512-byte LBA0 and LBA1: distinct ASCII tags, distant trailing sentinels,
# and otherwise zero bytes. This detects wrong offsets and short transfers.
printf '%s' 'NASEED-H4-NVME-LBA0-20261110' |
    dd of="$image" bs=1 seek=0 conv=notrunc status=none
printf '0' | dd of="$image" bs=1 seek=511 conv=notrunc status=none
printf '%s' 'NASEED-H4-NVME-LBA1-20261110' |
    dd of="$image" bs=1 seek=512 conv=notrunc status=none
printf '1' | dd of="$image" bs=1 seek=1023 conv=notrunc status=none

test "$(wc -c < "$image")" -eq 1048576 || {
    echo 'H4: fixture length differs from exactly 1 MiB' >&2
    exit 2
}
actual=$(sha256sum "$image" | awk '{print $1}')
if [ "$actual" != "$expected_sha" ]; then
    echo "H4: generated fixture hash mismatch: $actual" >&2
    exit 2
fi
echo "H4 seed created and verified: $actual"
