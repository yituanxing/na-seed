#!/bin/sh
# Run the 3 independent suites in a SINGLE NaSeed boot and collect several
# failures at once instead of stopping at the first unrelated userspace app.
set -eu
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
failures=0
for test_script in m59-rsync.sh m59-patch.sh m59-file.sh; do
    if /bin/sh "/tests/$test_script"; then
        echo "[M59 STAGE PASS] $test_script"
    else
        echo "[M59 STAGE FAIL] $test_script" >&2
        failures=$((failures + 1))
    fi
done
test "$failures" -eq 0 || {
    echo "[M59 FAIL] $failures independent program stages failed" >&2
    exit 1
}
echo M59_FULL_PASS
