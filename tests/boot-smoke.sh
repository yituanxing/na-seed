#!/bin/sh
# Only the fundamental serial boot contract; no M-number test forest.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 <iso>" >&2; exit 2; }
log=$(mktemp)
trap 'rm -f "$log"' 0
trap 'exit 2' 1 2 3 15
rc=0
timeout 45s sh scripts/run.sh "$1" >"$log" 2>&1 || rc=$?
if [ "$rc" -ne 1 ]; then
    cat "$log"
    echo "FAIL: QEMU returned $rc (expected isa-debug-exit=1)" >&2
    exit 1
fi
for marker in '[PASS] memory allocator' '[PASS] boot / serial / memory'; do
    if ! grep -Fq "$marker" "$log"; then
        cat "$log"
        echo "FAIL: missing serial marker: $marker" >&2
        exit 1
    fi
done
echo 'PASS: x86_64 UEFI boot and allocator smoke'
