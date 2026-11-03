#!/bin/sh
# Host fail-closed gate. Native coverage additionally requires a genuine
# system-QEMU guest + physical ext4 persisted bytes for each of three ISAs.
set -eu
mkdir -p build/m179
for source in scripts/m179-ext4-fixture.py scripts/probe-m179-fs-irq.sh; do
    test -s "$source" || {
        echo "[M179 HOST FAIL] missing actual test component: $source" >&2
        exit 1
    }
done
python3 -m py_compile scripts/m179-ext4-fixture.py
for tool in mke2fs debugfs e2fsck truncate; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "[M179 HOST FAIL] missing actual e2fsprogs fixture tool: $tool" >&2
        exit 1
    }
done
# The ordinary Host gate makes a real ext4 partition too, then insists that
# an image lacking the guest's physical write NEVER passes the persistence
# oracle. Preparation itself checks the independent 8192-byte seed bytes.
disk=build/m179/host-contract-16m.img
truncate -s 0 "$disk"
truncate -s 16M "$disk"
python3 scripts/m179-ext4-fixture.py prepare "$disk"
if python3 scripts/m179-ext4-fixture.py verify "$disk" \
        > build/m179/negative-missing-guest-write.log 2>&1; then
    echo '[M179 HOST FAIL] absent physical guest-written file falsely accepted' >&2
    exit 1
fi
echo '[M179 HOST NEGATIVE PASS] host ext4 seed verified, absent persisted guest write rejected'
for script in scripts/stage-m174-devfs-software-gui.sh \
              scripts/probe-m179-fs-irq.sh scripts/probe-m178-blk-irq.sh; do
    sh -n "$script"
done
sh scripts/check-m178-blk-irq.sh
echo '[M179 HOST FS IRQ PASS] physical ext4/MBR host persistence oracle, real kernel worker path, owned BlockRequest + M178/M177/M176/M175/M174 preservation'
