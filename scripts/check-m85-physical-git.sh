#!/bin/sh
# Independently inspect original Git's durable files from a raw ext4 disk.
# No guest serial marker, host-generated Git repo or CPIO file is accepted
# as a substitute for the physical block bytes at this checkpoint.
set -eu
[ "$#" -eq 4 ] || {
    echo 'usage: check-m85-physical-git.sh first|replay DISK WORK PORT' >&2
    exit 2
}
phase=$1 disk=$2 work=$3 port=$4
case "$phase" in first|replay) ;; *) exit 2 ;; esac
test -s "$disk" || { echo '[M85 PHYSICAL FAIL] missing raw ext4 disk' >&2; exit 1; }
mkdir -p "$work"
sh scripts/check-ext4-fsck-clean.sh "$disk"

prefix="$work/m85-$phase"
for item in result main-head topic-head pack-name; do
    debugfs -R "cat /m85-$item.txt" "$disk" > "$prefix-$item.txt" 2> "$prefix-$item.debugfs.log"
    test -s "$prefix-$item.txt" || {
        echo "[M85 PHYSICAL FAIL] $port missing ext4 /m85-$item.txt" >&2
        exit 1
    }
done
grep -Fxq 'NaSeed-M85-original-Git-persistent-ext4-PASS' "$prefix-result.txt" || {
    echo "[M85 PHYSICAL FAIL] $port missing actual guest durable marker" >&2
    exit 1
}
for name in main-head topic-head; do
    grep -Eq '^[0-9a-f]{40}([0-9a-f]{24})?$' "$prefix-$name.txt" || {
        echo "[M85 PHYSICAL FAIL] $port invalid Git commit SHA ($name)" >&2
        exit 1
    }
done
pack=$(cat "$prefix-pack-name.txt")
printf '%s\n' "$pack" | grep -Eq '^pack-[0-9a-f]{40}([0-9a-f]{24})?\.pack$' || {
    echo "[M85 PHYSICAL FAIL] $port malformed packed-object filename" >&2
    exit 1
}
debugfs -R 'cat /m85-git/.git/HEAD' "$disk" > "$prefix-git-HEAD.txt" 2> "$prefix-git-HEAD.debugfs.log"
grep -Eq '^ref: refs/heads/[A-Za-z0-9_-]+$' "$prefix-git-HEAD.txt" || {
    echo "[M85 PHYSICAL FAIL] $port missing real Git HEAD metadata" >&2
    exit 1
}

raw_pack="/m85-git/.git/objects/pack/$pack"
raw_idx="/m85-git/.git/objects/pack/${pack%.pack}.idx"
debugfs -R "dump $raw_pack $prefix-pack.pack" "$disk" > "$prefix-pack.debugfs.log" 2>&1
debugfs -R "dump $raw_idx $prefix-pack.idx" "$disk" > "$prefix-index.debugfs.log" 2>&1
test -s "$prefix-pack.pack" && test -s "$prefix-pack.idx" || {
    echo "[M85 PHYSICAL FAIL] $port missing raw Git pack/index" >&2
    exit 1
}
git verify-pack -v "$prefix-pack.idx" > "$prefix-verify-pack.txt" || {
    echo "[M85 PHYSICAL FAIL] $port independently invalid raw Git pack" >&2
    exit 1
}

if [ "$phase" = replay ]; then
    for item in result main-head topic-head pack-name git-HEAD; do
        cmp "$work/m85-first-$item.txt" "$prefix-$item.txt" || {
            echo "[M85 PHYSICAL FAIL] $port $item changed across real QEMU boots" >&2
            exit 1
        }
    done
    for extension in pack idx; do
        cmp "$work/m85-first-pack.$extension" "$prefix-pack.$extension" || {
            echo "[M85 PHYSICAL FAIL] $port original Git pack.$extension mutated across reboot" >&2
            exit 1
        }
    done
fi
sha256sum "$prefix-pack.pack" "$prefix-pack.idx" > "$prefix-physical-pack.sha256"
echo "[M85 PHYSICAL PASS] $port $phase signed Git object pack, durable refs, raw ext4, clean fsck"
