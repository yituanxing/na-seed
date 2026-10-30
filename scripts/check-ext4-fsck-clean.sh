#!/bin/sh
# Enforce a genuinely clean read-only e2fsck. e2fsck -fn can return zero
# while printing free block/inode count mismatches: status-only is not enough.
set -eu
test "$#" -eq 1 || { echo "usage: $0 <ext4-image>" >&2; exit 2; }
image=$1
test -s "$image"
tmp="${TMPDIR:-/tmp}/naseed-e2fsck-$$.log"
trap 'rm -f "$tmp"' EXIT HUP INT TERM
if ! e2fsck -fn "$image" >"$tmp" 2>&1; then
    cat "$tmp"
    echo "[EXT4 FSCK FAIL] e2fsck nonzero" >&2
    exit 1
fi
cat "$tmp"
if grep -Eiq 'count wrong|bitmap differences|UNEXPECTED INCONSISTENCY|fix\?|corrupt|invalid|wrong file type|orphan|multiply-claimed|not clean' "$tmp"; then
    echo "[EXT4 FSCK FAIL] filesystem metadata disagrees with on-disk allocation" >&2
    exit 1
fi
echo "[EXT4 FSCK CLEAN PASS] no allocation metadata inconsistencies"
