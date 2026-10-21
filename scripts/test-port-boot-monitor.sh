#!/bin/sh
# Fast POSIX host test: guest markers cannot be accepted independently.
set -eu
temp=$(mktemp -d)
pid=
cleanup() {
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :; fi
    rm -rf "$temp"
}
trap cleanup EXIT HUP INT TERM
(
    printf '%s\n' '[PASS] real boot'
    sleep 1
    printf '%s\n' '[PASS] real user VM'
    sleep 4
) > "$temp/boot.log" &
pid=$!
sh scripts/port-boot-until.sh "$temp/boot.log" "$pid" 5 \
    '[PASS] real boot' '[PASS] real user VM'
kill "$pid" 2>/dev/null || :
wait "$pid" 2>/dev/null || :
pid=
(
    printf '%s\n' '[PASS] real boot'
    printf '%s\n' '[FAIL] guest page-table corruption'
    sleep 3
) > "$temp/fail.log" &
pid=$!
if sh scripts/port-boot-until.sh "$temp/fail.log" "$pid" 5 \
   '[PASS] real boot' '[PASS] real user VM'; then
    echo '[FAIL] monitor incorrectly accepted a guest failure' >&2
    exit 1
fi
echo '[PASS] real-QEMU early-exit monitor accepts only complete clean boots'
