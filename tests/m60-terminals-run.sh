#!/bin/sh
# Exercise independent real programs even if the first one fails. Record both
# native Linux failures per guest boot, rather than a single artificial gate.
set -eu
PATH=/usr/bin:/bin
HOME=/tmp
TERM=vt100
LC_ALL=C
export PATH HOME TERM LC_ALL
failures=0
for program in less tmux; do
    if /bin/sh "/tests/m60-$program.sh"; then
        echo "[M60 STAGE PASS] $program"
    else
        echo "[M60 STAGE FAIL] $program" >&2
        failures=$((failures + 1))
    fi
done
test "$failures" -eq 0 || {
    echo "[M60 FAIL] $failures independent terminal workloads failed" >&2
    exit 1
}
echo M60_FULL_PASS
