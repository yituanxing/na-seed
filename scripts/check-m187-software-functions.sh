#!/bin/sh
# Host/static gate.  Numeric results and kernel inotify events are proved only
# by the guest script running in each real-ISA QEMU guest, not by this checker.
set -eu
sh scripts/check-m186-software-functions.sh
test -s tests/m187-software-functions.sh
sh -n tests/m187-software-functions.sh
grep -Fq 'tests/m187-software-functions.sh' scripts/m174-software-rootfs-copies.tsv
grep -Fq '/tests/m187-software-functions.sh' tests/m174-software-run.sh

for elf in usr/bin/sar usr/bin/sadf usr/bin/inotifywait; do
    grep -Fxq "$elf" scripts/m174-software-original-elfs.txt || {
        echo "[M187 SOFTWARE HOST FAIL] missing signed original: $elf" >&2
        exit 1
    }
done
test "$(grep -Ec '^[^#[:space:]]' scripts/m174-software-original-elfs.txt)" -eq 20

for marker in \
    M187_SOFTWARE_SAR_LIVE_CPU_FUNCTION_PASS \
    M187_SOFTWARE_SAR_BINARY_CAPTURE_FUNCTION_PASS \
    M187_SOFTWARE_SAR_BINARY_REPLAY_FUNCTION_PASS \
    M187_SOFTWARE_SADF_BINARY_DECODE_FUNCTION_PASS \
    M187_SOFTWARE_INOTIFY_CREATE_MODIFY_FUNCTION_PASS; do
    test "$(grep -Fxc "$marker" scripts/m174-expected-markers.txt)" -eq 1 || {
        echo "[M187 SOFTWARE HOST FAIL] expected marker missing or duplicated: $marker" >&2
        exit 1
    }
    test "$(grep -Fxc "echo $marker" tests/m187-software-functions.sh)" -eq 1 || {
        echo "[M187 SOFTWARE HOST FAIL] guest marker not unique: $marker" >&2
        exit 1
    }
done

grep -Fq '/usr/bin/sar -u 1 2' tests/m187-software-functions.sh
grep -Fq '/usr/bin/sar -u -o "$capture" 1 2' tests/m187-software-functions.sh
grep -Fq '/usr/bin/sar -u -f "$capture"' tests/m187-software-functions.sh
grep -Fq '/usr/bin/sadf -d "$capture" -- -u' tests/m187-software-functions.sh
grep -Fq '/usr/bin/inotifywait -q -e create' tests/m187-software-functions.sh
grep -Fq '/usr/bin/inotifywait -q -e modify' tests/m187-software-functions.sh
echo '[M187 SOFTWARE HOST CONTRACT PASS] existing 20 signed ELFs; sar/sadc/sadf real binary roundtrip + inotify CREATE/MODIFY staged'
